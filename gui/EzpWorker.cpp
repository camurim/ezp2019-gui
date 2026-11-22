#include "EzpWorker.h"

#include <sys/mman.h>
#include <unistd.h>

EzpWorker::EzpWorker(QObject *parent)
    : QObject(parent)
{
}

EzpWorker::~EzpWorker()
{
    shutdown();
}

void EzpWorker::requestAbort()
{
    m_abort = true;
}

void EzpWorker::initialize()
{
    if (m_handle != nullptr) {
        return;
    }

    const int res = exp2019_init(&m_handle);
    if (res != EXP2019_NO_ERROR) {
        emit logMessage(LogLevel::Error,
                        QStringLiteral("Falha ao inicializar libezp2019: %1")
                            .arg(QString::fromUtf8(exp2019_error_string(res))));
        return;
    }

    emit logMessage(LogLevel::Info, QStringLiteral("Worker USB inicializado via libezp2019.so (libusb-1.0)."));
}

void EzpWorker::shutdown()
{
    if (m_handle != nullptr) {
        exp2019_exit(m_handle);
        m_handle = nullptr;
    }
}

void EzpWorker::resetProgressThrottle()
{
    m_lastPercent = -1;
    m_progressTimer.restart();
}

void EzpWorker::progressTrampoline(size_t processed, size_t total, void *context)
{
    if (context != nullptr) {
        static_cast<EzpWorker *>(context)->emitProgressThrottled(processed, total);
    }
}

void EzpWorker::emitProgressThrottled(size_t processed, size_t total)
{
    if (total == 0) {
        return;
    }

    const int pct = static_cast<int>((processed * 100U) / total);
    const bool done = (processed >= total);
    if (done || pct != m_lastPercent || m_progressTimer.elapsed() >= 40) {
        m_lastPercent = pct;
        m_progressTimer.restart();
        emit progressChanged(static_cast<qint64>(processed), static_cast<qint64>(total));
    }
}

int EzpWorker::createMemoryFd(const QByteArray *initialData)
{
    const int fd = memfd_create("ezp2019_buffer", MFD_CLOEXEC);
    if (fd < 0) {
        return -1;
    }

    if (initialData != nullptr && !initialData->isEmpty()) {
        const char *ptr = initialData->constData();
        qsizetype remaining = initialData->size();
        while (remaining > 0) {
            const ssize_t written = ::write(fd, ptr, static_cast<size_t>(remaining));
            if (written < 0) {
                ::close(fd);
                return -1;
            }
            ptr += written;
            remaining -= written;
        }
        if (::lseek(fd, 0, SEEK_SET) < 0) {
            ::close(fd);
            return -1;
        }
    }

    return fd;
}

QByteArray EzpWorker::readAllFromFd(int fd)
{
    const off_t len = ::lseek(fd, 0, SEEK_END);
    if (len <= 0 || ::lseek(fd, 0, SEEK_SET) < 0) {
        return {};
    }

    QByteArray out;
    out.resize(static_cast<qsizetype>(len));
    char *dst = out.data();
    qsizetype remaining = out.size();
    while (remaining > 0) {
        const ssize_t n = ::read(fd, dst, static_cast<size_t>(remaining));
        if (n <= 0) {
            break;
        }
        dst += n;
        remaining -= n;
    }
    return out;
}

void EzpWorker::detectChip()
{
    if (m_handle == nullptr) {
        emit operationFinished(false, QStringLiteral("Biblioteca USB não inicializada."));
        return;
    }

    m_abort = false;
    emit operationStarted(QStringLiteral("Detectando chip..."));
    emit logMessage(LogLevel::Info, QStringLiteral("Consultando JEDEC ID do chip conectado..."));

    uint32_t chipId = 0;
    const int res = exp2019_connected_ic(m_handle, &chipId);
    exp2019_reset_ic(m_handle);

    if (res != EXP2019_NO_ERROR) {
        const QString err = QString::fromUtf8(exp2019_error_string(res));
        emit logMessage(LogLevel::Error, QStringLiteral("Falha na detecção: %1").arg(err));
        emit operationFinished(false, err);
        return;
    }

    emit chipDetected(chipId);
    emit operationFinished(true, QStringLiteral("Detecção concluída (ID: 0x%1)")
                                     .arg(chipId, 6, 16, QLatin1Char('0')).toUpper());
}

void EzpWorker::readChip(std::optional<std::size_t> chipIndex)
{
    if (m_handle == nullptr) {
        emit operationFinished(false, QStringLiteral("Biblioteca USB não inicializada."));
        return;
    }

    const int fd = createMemoryFd();
    if (fd < 0) {
        emit logMessage(LogLevel::Error, QStringLiteral("Falha ao criar buffer em memória (memfd)."));
        emit operationFinished(false, QStringLiteral("Erro ao alocar buffer temporário."));
        return;
    }

    m_abort = false;
    resetProgressThrottle();
    emit operationStarted(QStringLiteral("Lendo chip..."));
    emit logMessage(LogLevel::Info,
                    chipIndex.has_value()
                        ? QStringLiteral("Iniciando leitura usando o modelo selecionado (%1)...")
                              .arg(QString::fromUtf8(exp2019_get_chip_name_by_index(*chipIndex)))
                        : QStringLiteral("Iniciando leitura por autodetecção..."));

    const int res = chipIndex.has_value()
        ? exp2019_read_ic_by_index(m_handle, *chipIndex, fd, &EzpWorker::progressTrampoline, this, &m_abort)
        : exp2019_read_ic(m_handle, fd, &EzpWorker::progressTrampoline, this, &m_abort);
    exp2019_reset_ic(m_handle);

    if (res != EXP2019_NO_ERROR) {
        ::close(fd);
        const QString err = QString::fromUtf8(exp2019_error_string(res));
        const LogLevel lvl = (res == EXP2019_ABORTED) ? LogLevel::Warning : LogLevel::Error;
        emit logMessage(lvl, QStringLiteral("Leitura interrompida/falhou: %1").arg(err));
        emit operationFinished(false, err);
        return;
    }

    QByteArray data = readAllFromFd(fd);
    ::close(fd);

    emit progressChanged(data.size(), data.size());
    emit readCompleted(data);
    emit logMessage(LogLevel::Success,
                    QStringLiteral("Leitura concluída com sucesso (%1 bytes).").arg(data.size()));
    emit operationFinished(true, QStringLiteral("Leitura concluída."));
}

void EzpWorker::writeChip(const QByteArray &data, std::optional<std::size_t> chipIndex)
{
    if (m_handle == nullptr) {
        emit operationFinished(false, QStringLiteral("Biblioteca USB não inicializada."));
        return;
    }

    const int fd = createMemoryFd(&data);
    if (fd < 0) {
        emit logMessage(LogLevel::Error, QStringLiteral("Falha ao preparar buffer de gravação."));
        emit operationFinished(false, QStringLiteral("Erro ao preparar buffer."));
        return;
    }

    m_abort = false;
    resetProgressThrottle();
    emit operationStarted(QStringLiteral("Gravando chip..."));
    emit logMessage(LogLevel::Info,
                    QStringLiteral("Iniciando gravação (%1 bytes no buffer)...").arg(data.size()));

    const int res = chipIndex.has_value()
        ? exp2019_write_ic_by_index(m_handle, *chipIndex, fd, &EzpWorker::progressTrampoline, this, &m_abort)
        : exp2019_write_ic(m_handle, fd, &EzpWorker::progressTrampoline, this, &m_abort);
    exp2019_reset_ic(m_handle);
    ::close(fd);

    if (res != EXP2019_NO_ERROR) {
        const QString err = QString::fromUtf8(exp2019_error_string(res));
        const LogLevel lvl = (res == EXP2019_ABORTED) ? LogLevel::Warning : LogLevel::Error;
        emit logMessage(lvl, QStringLiteral("Gravação falhou: %1").arg(err));
        emit operationFinished(false, err);
        return;
    }

    emit progressChanged(data.size(), data.size());
    emit logMessage(LogLevel::Success, QStringLiteral("Gravação concluída com sucesso."));
    emit operationFinished(true, QStringLiteral("Gravação concluída."));
}

void EzpWorker::verifyChip(const QByteArray &data, std::optional<std::size_t> chipIndex)
{
    if (m_handle == nullptr) {
        emit operationFinished(false, QStringLiteral("Biblioteca USB não inicializada."));
        return;
    }

    const int fd = createMemoryFd(&data);
    if (fd < 0) {
        emit logMessage(LogLevel::Error, QStringLiteral("Falha ao preparar buffer de verificação."));
        emit operationFinished(false, QStringLiteral("Erro ao preparar buffer."));
        return;
    }

    m_abort = false;
    resetProgressThrottle();
    emit operationStarted(QStringLiteral("Verificando chip..."));
    emit logMessage(LogLevel::Info, QStringLiteral("Iniciando verificação do chip contra o buffer..."));

    bool matched = false;
    const int res = chipIndex.has_value()
        ? exp2019_verify_ic_by_index(m_handle, *chipIndex, fd, &EzpWorker::progressTrampoline, this, &m_abort, &matched)
        : exp2019_verify_ic(m_handle, fd, &EzpWorker::progressTrampoline, this, &m_abort, &matched);
    exp2019_reset_ic(m_handle);
    ::close(fd);

    if (res != EXP2019_NO_ERROR) {
        const QString err = QString::fromUtf8(exp2019_error_string(res));
        const LogLevel lvl = (res == EXP2019_ABORTED) ? LogLevel::Warning : LogLevel::Error;
        emit logMessage(lvl, QStringLiteral("Verificação falhou: %1").arg(err));
        emit operationFinished(false, err);
        return;
    }

    emit progressChanged(data.size(), data.size());
    emit verifyCompleted(matched);
    if (matched) {
        emit logMessage(LogLevel::Success, QStringLiteral("Verificação OK: conteúdo idêntico ao buffer!"));
        emit operationFinished(true, QStringLiteral("Dados verificados (OK)."));
    } else {
        emit logMessage(LogLevel::Error, QStringLiteral("Verificação falhou: divergência encontrada nos dados!"));
        emit operationFinished(false, QStringLiteral("Dados divergentes."));
    }
}

void EzpWorker::eraseChip(std::optional<std::size_t> chipIndex)
{
    if (m_handle == nullptr) {
        emit operationFinished(false, QStringLiteral("Biblioteca USB não inicializada."));
        return;
    }

    m_abort = false;
    emit operationStarted(QStringLiteral("Apagando chip..."));
    emit logMessage(LogLevel::Info, QStringLiteral("Enviando comando de apagamento total do chip..."));

    const int res = chipIndex.has_value()
        ? exp2019_erase_ic_by_index(m_handle, *chipIndex, &m_abort)
        : exp2019_erase_ic(m_handle, &m_abort);
    exp2019_reset_ic(m_handle);

    if (res != EXP2019_NO_ERROR) {
        const QString err = QString::fromUtf8(exp2019_error_string(res));
        const LogLevel lvl = (res == EXP2019_ABORTED) ? LogLevel::Warning : LogLevel::Error;
        emit logMessage(lvl, QStringLiteral("Apagamento falhou: %1").arg(err));
        emit operationFinished(false, err);
        return;
    }

    emit progressChanged(100, 100);
    emit logMessage(LogLevel::Success, QStringLiteral("Chip apagado com sucesso."));
    emit operationFinished(true, QStringLiteral("Chip apagado."));
}

void EzpWorker::autoProgram(const QByteArray &data, std::optional<std::size_t> chipIndex)
{
    if (m_handle == nullptr) {
        emit operationFinished(false, QStringLiteral("Biblioteca USB não inicializada."));
        return;
    }

    m_abort = false;
    emit operationStarted(QStringLiteral("Auto (1/3 Apagando)..."));
    emit logMessage(LogLevel::Info, QStringLiteral("[Auto 1/3] Apagando chip..."));

    int res = chipIndex.has_value()
        ? exp2019_erase_ic_by_index(m_handle, *chipIndex, &m_abort)
        : exp2019_erase_ic(m_handle, &m_abort);
    if (res != EXP2019_NO_ERROR) {
        exp2019_reset_ic(m_handle);
        const QString err = QString::fromUtf8(exp2019_error_string(res));
        emit logMessage(LogLevel::Error, QStringLiteral("[Auto 1/3] Falha ao apagar: %1").arg(err));
        emit operationFinished(false, err);
        return;
    }
    emit logMessage(LogLevel::Success, QStringLiteral("[Auto 1/3] Apagamento concluído."));

    const int writeFd = createMemoryFd(&data);
    if (writeFd < 0) {
        exp2019_reset_ic(m_handle);
        emit operationFinished(false, QStringLiteral("Erro ao preparar buffer."));
        return;
    }

    resetProgressThrottle();
    emit operationStarted(QStringLiteral("Auto (2/3 Gravando)..."));
    emit logMessage(LogLevel::Info, QStringLiteral("[Auto 2/3] Gravando chip..."));
    res = chipIndex.has_value()
        ? exp2019_write_ic_by_index(m_handle, *chipIndex, writeFd, &EzpWorker::progressTrampoline, this, &m_abort)
        : exp2019_write_ic(m_handle, writeFd, &EzpWorker::progressTrampoline, this, &m_abort);
    ::close(writeFd);
    if (res != EXP2019_NO_ERROR) {
        exp2019_reset_ic(m_handle);
        const QString err = QString::fromUtf8(exp2019_error_string(res));
        emit logMessage(LogLevel::Error, QStringLiteral("[Auto 2/3] Falha na gravação: %1").arg(err));
        emit operationFinished(false, err);
        return;
    }
    emit logMessage(LogLevel::Success, QStringLiteral("[Auto 2/3] Gravação concluída."));

    const int verifyFd = createMemoryFd(&data);
    if (verifyFd < 0) {
        exp2019_reset_ic(m_handle);
        emit operationFinished(false, QStringLiteral("Erro ao preparar buffer de verificação."));
        return;
    }

    resetProgressThrottle();
    emit operationStarted(QStringLiteral("Auto (3/3 Verificando)..."));
    emit logMessage(LogLevel::Info, QStringLiteral("[Auto 3/3] Verificando dados gravados..."));
    bool matched = false;
    res = chipIndex.has_value()
        ? exp2019_verify_ic_by_index(m_handle, *chipIndex, verifyFd, &EzpWorker::progressTrampoline, this, &m_abort, &matched)
        : exp2019_verify_ic(m_handle, verifyFd, &EzpWorker::progressTrampoline, this, &m_abort, &matched);
    exp2019_reset_ic(m_handle);
    ::close(verifyFd);

    if (res != EXP2019_NO_ERROR) {
        const QString err = QString::fromUtf8(exp2019_error_string(res));
        emit logMessage(LogLevel::Error, QStringLiteral("[Auto 3/3] Falha na verificação: %1").arg(err));
        emit operationFinished(false, err);
        return;
    }

    emit progressChanged(data.size(), data.size());
    emit verifyCompleted(matched);
    if (matched) {
        emit logMessage(LogLevel::Success, QStringLiteral("[Auto 3/3] Sequência concluída: chip gravado e verificado com sucesso!"));
        emit operationFinished(true, QStringLiteral("Sequência Auto concluída (OK)."));
    } else {
        emit logMessage(LogLevel::Error, QStringLiteral("[Auto 3/3] Falha: os dados gravados não conferem com o buffer!"));
        emit operationFinished(false, QStringLiteral("Auto falhou na verificação."));
    }
}
