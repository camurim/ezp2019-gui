#pragma once

#include "ezp2019.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <optional>

class EzpWorker : public QObject {
    Q_OBJECT

public:
    enum class LogLevel {
        Info,
        Success,
        Warning,
        Error
    };
    Q_ENUM(LogLevel)

    explicit EzpWorker(QObject *parent = nullptr);
    ~EzpWorker() override;

    void requestAbort();

public slots:
    void initialize();
    void shutdown();
    void detectChip();
    void readChip(std::optional<std::size_t> chipIndex);
    void writeChip(const QByteArray &data, std::optional<std::size_t> chipIndex);
    void verifyChip(const QByteArray &data, std::optional<std::size_t> chipIndex);
    void eraseChip(std::optional<std::size_t> chipIndex);
    void autoProgram(const QByteArray &data, std::optional<std::size_t> chipIndex);

signals:
    void operationStarted(const QString &operationName);
    void operationFinished(bool success, const QString &summary);
    void progressChanged(qint64 processed, qint64 total);
    void logMessage(EzpWorker::LogLevel level, const QString &message);
    void chipDetected(uint32_t chipId);
    void readCompleted(const QByteArray &data);
    void verifyCompleted(bool matched);

private:
    static void progressTrampoline(size_t processed, size_t total, void *context);
    void emitProgressThrottled(size_t processed, size_t total);
    void resetProgressThrottle();
    int createMemoryFd(const QByteArray *initialData = nullptr);
    QByteArray readAllFromFd(int fd);

    exp2019 m_handle = nullptr;
    volatile bool m_abort = false;
    QElapsedTimer m_progressTimer;
    int m_lastPercent = -1;
};
