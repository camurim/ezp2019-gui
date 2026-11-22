#include "MainWindow.h"

#include "BufferUtils.h"
#include "HexEditorWidget.h"
#include "ZifSocketWidget.h"

#include <QAction>
#include <QComboBox>
#include <QCompleter>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QStringListModel>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("EZP2019 USB Programmer"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/ezp2019-gui.svg")));
    resize(1180, 760);
    setMinimumSize(940, 620);

    setupToolbar();
    setupUi();
    setupStatusBar();
    populateMemoryTypes();
    setupWorker();

    appendLog(EzpWorker::LogLevel::Info,
              QStringLiteral("Base de chips carregada: %1 modelos suportados.").arg(m_chipDb.allChips().size()));
}

MainWindow::~MainWindow()
{
    if (m_usbMonitor != nullptr) {
        m_usbMonitor->stop();
    }
    if (m_worker != nullptr) {
        m_worker->requestAbort();
        QMetaObject::invokeMethod(m_worker, &EzpWorker::shutdown, Qt::BlockingQueuedConnection);
    }
    m_workerThread.quit();
    m_workerThread.wait(3000);
}

void MainWindow::setupToolbar()
{
    auto *tb = addToolBar(QStringLiteral("Operações"));
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    tb->setIconSize(QSize(28, 28));

    m_actOpen = tb->addAction(
        QIcon(QStringLiteral(":/icons/open.svg")),
        QStringLiteral("Abrir Arquivo"), this, &MainWindow::onOpenFile);
    m_actOpen->setShortcut(QKeySequence::Open);

    m_actSave = tb->addAction(
        QIcon(QStringLiteral(":/icons/save.svg")),
        QStringLiteral("Salvar Arquivo"), this, &MainWindow::onSaveFile);
    m_actSave->setShortcut(QKeySequence::Save);

    tb->addSeparator();

    m_actDetect = tb->addAction(
        QIcon(QStringLiteral(":/icons/detect.svg")),
        QStringLiteral("Detectar Chip"), this, &MainWindow::onDetectChip);

    m_actRead = tb->addAction(
        QIcon(QStringLiteral(":/icons/read.svg")),
        QStringLiteral("Ler Chip"), this, &MainWindow::onReadChip);

    m_actWrite = tb->addAction(
        QIcon(QStringLiteral(":/icons/write.svg")),
        QStringLiteral("Gravar Chip"), this, &MainWindow::onWriteChip);

    m_actVerify = tb->addAction(
        QIcon(QStringLiteral(":/icons/verify.svg")),
        QStringLiteral("Verificar"), this, &MainWindow::onVerifyChip);

    m_actErase = tb->addAction(
        QIcon(QStringLiteral(":/icons/erase.svg")),
        QStringLiteral("Apagar"), this, &MainWindow::onEraseChip);

    tb->addSeparator();

    m_actAuto = tb->addAction(
        QIcon(QStringLiteral(":/icons/auto.svg")),
        QStringLiteral("Auto"), this, &MainWindow::onAutoSequence);
    m_actAuto->setToolTip(QStringLiteral("Executar sequência: Apagar → Gravar → Verificar"));

    tb->addSeparator();

    m_actCancel = tb->addAction(
        QIcon(QStringLiteral(":/icons/cancel.svg")),
        QStringLiteral("Cancelar"), this, &MainWindow::onCancelOperation);
    m_actCancel->setEnabled(false);
}

void MainWindow::setupUi()
{
    auto *central = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(8, 8, 8, 8);
    rootLayout->setSpacing(6);

    // Horizontal main splitter: Left Panel | Central Hex Area
    auto *mainSplitter = new QSplitter(Qt::Horizontal, central);

    // --- Left Panel (Hardware Status + Chip Selectors + ZIF Diagram) ---
    auto *leftPanel = new QWidget(mainSplitter);
    leftPanel->setMinimumWidth(285);
    leftPanel->setMaximumWidth(350);
    auto *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 4, 0);
    leftLayout->setSpacing(8);

    // 1. Hardware Status Box
    auto *hwGroup = new QGroupBox(QStringLiteral("Programador EZP2019"), leftPanel);
    auto *hwOuterLayout = new QVBoxLayout(hwGroup);
    hwOuterLayout->setSpacing(6);
    auto *hwLayout = new QHBoxLayout();
    m_statusLed = new QLabel(hwGroup);
    m_statusLed->setFixedSize(14, 14);
    m_statusText = new QLabel(QStringLiteral("Desconectado"), hwGroup);
    QFont boldFont = font();
    boldFont.setBold(true);
    m_statusText->setFont(boldFont);
    hwLayout->addWidget(m_statusLed);
    hwLayout->addWidget(m_statusText, 1);
    hwOuterLayout->addLayout(hwLayout);

    m_fixUdevBtn = new QPushButton(QStringLiteral("Instalar regra udev (pkexec)..."), hwGroup);
    m_fixUdevBtn->setVisible(false);
    connect(m_fixUdevBtn, &QPushButton::clicked, this, &MainWindow::onInstallUdevRule);
    hwOuterLayout->addWidget(m_fixUdevBtn);

    leftLayout->addWidget(hwGroup);
    onUsbStateChanged(UsbConnectionState::Disconnected);

    // 2. Chip Selection Box
    auto *chipGroup = new QGroupBox(QStringLiteral("Seleção de Chip"), leftPanel);
    auto *chipLayout = new QVBoxLayout(chipGroup);
    chipLayout->setSpacing(6);

    m_searchEdit = new QLineEdit(chipGroup);
    m_searchEdit->setPlaceholderText(QStringLiteral("Busca rápida (ex: W25Q64, AT24C256)..."));
    m_searchEdit->setClearButtonEnabled(true);

    QStringList searchItems;
    searchItems.reserve(m_chipDb.allChips().size());
    for (const auto &c : m_chipDb.allChips()) {
        searchItems.push_back(QStringLiteral("%1 — %2 (%3)").arg(c.name, c.manufacturer, c.displayType));
    }
    auto *completer = new QCompleter(searchItems, m_searchEdit);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    m_searchEdit->setCompleter(completer);
    connect(completer, QOverload<const QString &>::of(&QCompleter::activated),
            this, &MainWindow::onQuickSearchActivated);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, [this]() {
        onQuickSearchActivated(m_searchEdit->text());
    });
    chipLayout->addWidget(m_searchEdit);

    auto *formLayout = new QFormLayout();
    formLayout->setContentsMargins(0, 4, 0, 4);
    formLayout->setSpacing(6);

    m_typeCombo = new QComboBox(chipGroup);
    m_manufCombo = new QComboBox(chipGroup);
    m_chipCombo = new QComboBox(chipGroup);

    connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onMemoryTypeChanged);
    connect(m_manufCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onManufacturerChanged);
    connect(m_chipCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onChipModelChanged);

    formLayout->addRow(QStringLiteral("Tipo:"), m_typeCombo);
    formLayout->addRow(QStringLiteral("Fabricante:"), m_manufCombo);
    formLayout->addRow(QStringLiteral("Modelo:"), m_chipCombo);
    chipLayout->addLayout(formLayout);

    leftLayout->addWidget(chipGroup);

    // 3. ZIF Socket Diagram Box (SVG dinâmico + seletor de encapsulamento)
    auto *zifGroup = new QGroupBox(QStringLiteral("Encaixe no Soquete ZIF"), leftPanel);
    auto *zifLayout = new QVBoxLayout(zifGroup);
    zifLayout->setContentsMargins(6, 6, 6, 6);
    zifLayout->setSpacing(6);

    auto *pkgRow = new QHBoxLayout();
    auto *pkgLabel = new QLabel(QStringLiteral("Encapsulamento:"), zifGroup);
    m_packageCombo = new QComboBox(zifGroup);
    m_packageCombo->addItem(QStringLiteral("DIP-8 (Direto)"), static_cast<int>(ChipPackage::Dip8));
    m_packageCombo->addItem(QStringLiteral("SOIC-8 (Adaptador)"), static_cast<int>(ChipPackage::Soic8Adapter));
    connect(m_packageCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onPackageChanged);
    pkgRow->addWidget(pkgLabel);
    pkgRow->addWidget(m_packageCombo, 1);
    zifLayout->addLayout(pkgRow);

    m_zifWidget = new ZifSocketWidget(zifGroup);
    zifLayout->addWidget(m_zifWidget, 1);
    leftLayout->addWidget(zifGroup, 1);

    // 4. Technical Specifications Box (abaixo do diagrama do soquete)
    auto *specGroup = new QGroupBox(QStringLiteral("Especificações Técnicas"), leftPanel);
    auto *specForm = new QFormLayout(specGroup);
    specForm->setContentsMargins(8, 6, 8, 6);
    specForm->setSpacing(3);
    m_specSize = new QLabel(QStringLiteral("-"), specGroup);
    m_specPage = new QLabel(QStringLiteral("-"), specGroup);
    m_specVoltage = new QLabel(QStringLiteral("-"), specGroup);
    m_specId = new QLabel(QStringLiteral("-"), specGroup);
    m_specProtocol = new QLabel(QStringLiteral("-"), specGroup);
    m_specSize->setWordWrap(true);
    m_specVoltage->setWordWrap(true);
    specForm->addRow(QStringLiteral("Capacidade:"), m_specSize);
    specForm->addRow(QStringLiteral("Página:"), m_specPage);
    specForm->addRow(QStringLiteral("Voltagem:"), m_specVoltage);
    specForm->addRow(QStringLiteral("JEDEC ID:"), m_specId);
    specForm->addRow(QStringLiteral("Protocolo:"), m_specProtocol);
    leftLayout->addWidget(specGroup);

    mainSplitter->addWidget(leftPanel);

    // --- Right Area (Central Hex Editor + Bottom Progress & Log) ---
    auto *rightSplitter = new QSplitter(Qt::Vertical, mainSplitter);

    // Hex editor container
    auto *hexContainer = new QWidget(rightSplitter);
    auto *hexLayout = new QVBoxLayout(hexContainer);
    hexLayout->setContentsMargins(4, 0, 0, 4);
    hexLayout->setSpacing(4);

    // Buffer tools bar (Fill 0xFF / 0x00 + Hex/ASCII Search)
    auto *bufToolsRow = new QHBoxLayout();
    bufToolsRow->setSpacing(6);

    auto *fillFFBtn = new QPushButton(QStringLiteral("Preencher 0xFF"), hexContainer);
    auto *fill00Btn = new QPushButton(QStringLiteral("Preencher 0x00"), hexContainer);
    connect(fillFFBtn, &QPushButton::clicked, this, &MainWindow::onFillBufferFF);
    connect(fill00Btn, &QPushButton::clicked, this, &MainWindow::onFillBuffer00);

    m_hexSearchMode = new QComboBox(hexContainer);
    m_hexSearchMode->addItem(QStringLiteral("Hex"), 0);
    m_hexSearchMode->addItem(QStringLiteral("ASCII"), 1);
    m_hexSearchMode->setFixedWidth(74);

    m_hexSearchEdit = new QLineEdit(hexContainer);
    m_hexSearchEdit->setPlaceholderText(QStringLiteral("Buscar no buffer (ex: EF 40 17 ou texto)..."));
    m_hexSearchEdit->setClearButtonEnabled(true);
    connect(m_hexSearchEdit, &QLineEdit::returnPressed, this, &MainWindow::onFindNext);

    auto *findPrevBtn = new QPushButton(QStringLiteral("Anterior"), hexContainer);
    auto *findNextBtn = new QPushButton(QStringLiteral("Buscar"), hexContainer);
    connect(findPrevBtn, &QPushButton::clicked, this, &MainWindow::onFindPrevious);
    connect(findNextBtn, &QPushButton::clicked, this, &MainWindow::onFindNext);

    bufToolsRow->addWidget(fillFFBtn);
    bufToolsRow->addWidget(fill00Btn);
    bufToolsRow->addSpacing(8);
    bufToolsRow->addWidget(m_hexSearchMode);
    bufToolsRow->addWidget(m_hexSearchEdit, 1);
    bufToolsRow->addWidget(findPrevBtn);
    bufToolsRow->addWidget(findNextBtn);
    hexLayout->addLayout(bufToolsRow);

    auto *infoBar = new QHBoxLayout();
    m_bufferInfoLabel = new QLabel(QStringLiteral("Buffer: vazio (0 bytes)"), hexContainer);
    m_cursorInfoLabel = new QLabel(QStringLiteral("Cursor: -"), hexContainer);
    infoBar->addWidget(m_bufferInfoLabel, 1);
    infoBar->addWidget(m_cursorInfoLabel);
    hexLayout->addLayout(infoBar);

    m_hexEditor = new HexEditorWidget(hexContainer);
    connect(m_hexEditor, &HexEditorWidget::cursorChanged, this, &MainWindow::onCursorChanged);
    connect(m_hexEditor, &HexEditorWidget::dataModified, this, &MainWindow::onBufferDataModified);
    hexLayout->addWidget(m_hexEditor, 1);

    rightSplitter->addWidget(hexContainer);

    // Bottom Progress + Log Panel
    auto *bottomContainer = new QWidget(rightSplitter);
    auto *bottomLayout = new QVBoxLayout(bottomContainer);
    bottomLayout->setContentsMargins(4, 4, 0, 0);
    bottomLayout->setSpacing(4);

    auto *progressRow = new QHBoxLayout();
    m_progressLabel = new QLabel(QStringLiteral("Pronto."), bottomContainer);
    m_progressLabel->setMinimumWidth(180);
    m_progressBar = new QProgressBar(bottomContainer);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    auto *clearLogBtn = new QPushButton(QStringLiteral("Limpar Log"), bottomContainer);
    clearLogBtn->setFixedWidth(96);
    connect(clearLogBtn, &QPushButton::clicked, this, [this]() {
        m_logView->clear();
    });

    progressRow->addWidget(m_progressLabel);
    progressRow->addWidget(m_progressBar, 1);
    progressRow->addWidget(clearLogBtn);
    bottomLayout->addLayout(progressRow);

    m_logView = new QPlainTextEdit(bottomContainer);
    m_logView->setReadOnly(true);
    m_logView->setMaximumBlockCount(2000);
    QFont logFont(QStringLiteral("Monospace"));
    logFont.setStyleHint(QFont::TypeWriter);
    logFont.setPointSize(9);
    m_logView->setFont(logFont);
    bottomLayout->addWidget(m_logView, 1);

    rightSplitter->addWidget(bottomContainer);
    rightSplitter->setStretchFactor(0, 4);
    rightSplitter->setStretchFactor(1, 1);
    rightSplitter->setSizes({520, 170});

    mainSplitter->addWidget(rightSplitter);
    mainSplitter->setStretchFactor(0, 0);
    mainSplitter->setStretchFactor(1, 1);

    rootLayout->addWidget(mainSplitter, 1);
    setCentralWidget(central);
}

void MainWindow::setupStatusBar()
{
    m_statusCrcLabel = new QLabel(QStringLiteral("CRC32: --------"), this);
    m_statusMd5Label = new QLabel(QStringLiteral("MD5: --------------------------------"), this);
    m_statusModLabel = new QLabel(QStringLiteral("Modificados: 0"), this);

    QFont mono(QStringLiteral("Monospace"));
    mono.setStyleHint(QFont::TypeWriter);
    mono.setPointSize(9);
    m_statusCrcLabel->setFont(mono);
    m_statusMd5Label->setFont(mono);
    m_statusModLabel->setFont(mono);

    m_statusCrcLabel->setContentsMargins(6, 0, 10, 0);
    m_statusMd5Label->setContentsMargins(6, 0, 10, 0);
    m_statusModLabel->setContentsMargins(6, 0, 6, 0);

    statusBar()->addWidget(m_statusCrcLabel);
    statusBar()->addWidget(m_statusMd5Label, 1);
    statusBar()->addPermanentWidget(m_statusModLabel);

    m_checksumDebounceTimer = new QTimer(this);
    m_checksumDebounceTimer->setSingleShot(true);
    m_checksumDebounceTimer->setInterval(150);
    connect(m_checksumDebounceTimer, &QTimer::timeout, this, &MainWindow::updateBufferChecksums);
}

void MainWindow::setupWorker()
{
    m_worker = new EzpWorker();
    m_worker->moveToThread(&m_workerThread);
    m_workerThread.setObjectName(QStringLiteral("ezp2019-io-worker"));

    connect(&m_workerThread, &QThread::started, m_worker, &EzpWorker::initialize);
    connect(&m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);

    connect(m_worker, &EzpWorker::operationStarted, this, &MainWindow::onOperationStarted);
    connect(m_worker, &EzpWorker::operationFinished, this, &MainWindow::onOperationFinished);
    connect(m_worker, &EzpWorker::progressChanged, this, &MainWindow::onProgressChanged);
    connect(m_worker, &EzpWorker::logMessage, this, &MainWindow::onLogMessage);
    connect(m_worker, &EzpWorker::chipDetected, this, &MainWindow::onChipDetected);
    connect(m_worker, &EzpWorker::readCompleted, this, &MainWindow::onReadCompleted);

    m_workerThread.start();

    // Continuous USB connection monitor in its own dedicated polling thread (every 1.5s)
    m_usbMonitor = new UsbMonitor(1500, this);
    connect(m_usbMonitor, &UsbMonitor::connectionStateChanged, this, &MainWindow::onUsbStateChanged);
    m_usbMonitor->start();
}

void MainWindow::populateMemoryTypes()
{
    m_typeCombo->blockSignals(true);
    m_typeCombo->clear();
    m_typeCombo->addItems(m_chipDb.memoryTypes());
    m_typeCombo->blockSignals(false);

    onMemoryTypeChanged(m_typeCombo->currentIndex());
}

void MainWindow::onMemoryTypeChanged(int)
{
    const QString type = m_typeCombo->currentText();
    const QStringList manufs = m_chipDb.manufacturersForType(type);

    m_manufCombo->blockSignals(true);
    m_manufCombo->clear();
    m_manufCombo->addItems(manufs);
    m_manufCombo->blockSignals(false);

    onManufacturerChanged(m_manufCombo->currentIndex());
}

void MainWindow::onManufacturerChanged(int)
{
    const QString type = m_typeCombo->currentText();
    const QString manuf = m_manufCombo->currentText();
    const auto chips = m_chipDb.chipsFor(type, manuf);

    m_chipCombo->blockSignals(true);
    m_chipCombo->clear();
    for (const auto &c : chips) {
        m_chipCombo->addItem(c.name, QVariant::fromValue(static_cast<qulonglong>(c.index)));
    }
    m_chipCombo->blockSignals(false);

    onChipModelChanged(m_chipCombo->currentIndex());
}

void MainWindow::onChipModelChanged(int index)
{
    if (index < 0) {
        m_selectedChip = std::nullopt;
        updateChipSpecLabels(m_selectedChip);
        m_zifWidget->setChip(m_selectedChip);
        m_hexEditor->clear();
        m_bufferInfoLabel->setText(QStringLiteral("Buffer: vazio (0 bytes)"));
        updateBufferChecksums();
        return;
    }

    const auto chipIdx = static_cast<std::size_t>(m_chipCombo->itemData(index).toULongLong());
    m_selectedChip = m_chipDb.findByIndex(chipIdx);
    updateChipSpecLabels(m_selectedChip);
    m_zifWidget->setChip(m_selectedChip);

    if (m_selectedChip.has_value() && m_selectedChip->size > 0) {
        m_currentFilePath.clear();
        m_hexEditor->fill(0xFF, static_cast<qsizetype>(m_selectedChip->size));
        m_bufferInfoLabel->setText(
            QStringLiteral("Buffer: inicializado 0xFF (%1 — %2)")
                .arg(m_selectedChip->name, m_selectedChip->formattedCapacity()));
        updateBufferChecksums();
    }
}

void MainWindow::onPackageChanged(int index)
{
    if (index < 0 || m_zifWidget == nullptr) {
        return;
    }
    const auto pkg = static_cast<ChipPackage>(m_packageCombo->itemData(index).toInt());
    m_zifWidget->setPackage(pkg);
}

void MainWindow::onQuickSearchActivated(const QString &text)
{
    const QString clean = text.section(QStringLiteral(" — "), 0, 0).trimmed();
    if (clean.isEmpty()) {
        return;
    }

    const auto matches = m_chipDb.searchByCode(clean);
    if (matches.isEmpty()) {
        appendLog(EzpWorker::LogLevel::Warning,
                  QStringLiteral("Nenhum chip encontrado para \"%1\".").arg(clean));
        return;
    }

    // Prefer exact name match if present
    for (const auto &c : matches) {
        if (c.name.compare(clean, Qt::CaseInsensitive) == 0) {
            selectChip(c);
            return;
        }
    }
    selectChip(matches.first());
}

void MainWindow::selectChip(const ChipInfo &chip)
{
    const int typeIdx = m_typeCombo->findText(chip.displayType);
    if (typeIdx >= 0) {
        m_typeCombo->setCurrentIndex(typeIdx);
    }

    const int manufIdx = m_manufCombo->findText(chip.manufacturer);
    if (manufIdx >= 0) {
        m_manufCombo->setCurrentIndex(manufIdx);
    }

    const int chipIdx = m_chipCombo->findData(QVariant::fromValue(static_cast<qulonglong>(chip.index)));
    if (chipIdx >= 0) {
        m_chipCombo->setCurrentIndex(chipIdx);
    }
}

void MainWindow::updateChipSpecLabels(const std::optional<ChipInfo> &chip)
{
    if (!chip.has_value()) {
        m_specSize->setText(QStringLiteral("-"));
        m_specPage->setText(QStringLiteral("-"));
        m_specVoltage->setText(QStringLiteral("-"));
        m_specId->setText(QStringLiteral("-"));
        m_specProtocol->setText(QStringLiteral("-"));
        return;
    }

    m_specSize->setText(chip->formattedCapacity());
    m_specPage->setText(QStringLiteral("%1 Bytes").arg(chip->pageSize));
    m_specVoltage->setText(chip->voltageLabel());
    m_specId->setText(chip->chipId == 0
                          ? QStringLiteral("N/A (sem JEDEC ID)")
                          : QStringLiteral("0x%1").arg(chip->chipId, 6, 16, QLatin1Char('0')).toUpper());
    m_specProtocol->setText(chip->protocolName());
}

void MainWindow::onOpenFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Abrir arquivo (.bin / Intel .hex)"),
        m_currentFilePath,
        QStringLiteral("Imagens Suportadas (*.bin *.hex *.ihx *.rom *.img);;Binário (*.bin *.rom *.img);;Intel HEX (*.hex *.ihx);;Todos os arquivos (*)"));

    if (path.isEmpty()) {
        return;
    }

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, QStringLiteral("Erro ao abrir arquivo"), f.errorString());
        return;
    }

    const QByteArray raw = f.readAll();
    QByteArray loadedData;
    QString formatTag = QStringLiteral("BIN");

    if (BufferUtils::isLikelyIntelHex(path, raw)) {
        const IntelHexResult parsed = BufferUtils::parseIntelHex(raw);
        if (!parsed.ok) {
            QMessageBox::critical(this, QStringLiteral("Erro ao ler Intel HEX"), parsed.errorMessage);
            appendLog(EzpWorker::LogLevel::Error,
                      QStringLiteral("Falha no parse Intel HEX (%1): %2").arg(path, parsed.errorMessage));
            return;
        }
        loadedData = parsed.data;
        formatTag = QStringLiteral("Intel HEX");
    } else {
        loadedData = raw;
    }

    const qsizetype originalSize = loadedData.size();

    if (m_selectedChip.has_value() && m_selectedChip->size > 0) {
        const qsizetype chipSize = static_cast<qsizetype>(m_selectedChip->size);
        if (originalSize > chipSize) {
            const auto ans = QMessageBox::warning(
                this,
                QStringLiteral("Arquivo maior que a capacidade do chip"),
                QStringLiteral(
                    "O arquivo possui %1 bytes, mas o chip selecionado (%2) suporta apenas %3 bytes.\n\n"
                    "Deseja truncar os dados para caber na capacidade do chip?")
                    .arg(originalSize)
                    .arg(m_selectedChip->name)
                    .arg(chipSize),
                QMessageBox::Yes | QMessageBox::Cancel,
                QMessageBox::Yes);

            if (ans != QMessageBox::Yes) {
                return;
            }
            loadedData.truncate(chipSize);
            appendLog(EzpWorker::LogLevel::Warning,
                      QStringLiteral("Arquivo truncado de %1 para %2 bytes (capacidade do chip %3).")
                          .arg(originalSize).arg(chipSize).arg(m_selectedChip->name));
        } else if (originalSize < chipSize) {
            QMessageBox::information(
                this,
                QStringLiteral("Arquivo menor que a capacidade do chip"),
                QStringLiteral(
                    "O arquivo possui %1 bytes, menor que a capacidade de %2 bytes do chip selecionado (%3).\n\n"
                    "O restante do buffer será preenchido automaticamente com 0xFF.")
                    .arg(originalSize)
                    .arg(chipSize)
                    .arg(m_selectedChip->name));

            const qsizetype padBytes = chipSize - originalSize;
            loadedData.append(QByteArray(padBytes, static_cast<char>(0xFF)));
            appendLog(EzpWorker::LogLevel::Warning,
                      QStringLiteral("Arquivo menor que o chip (%1 < %2 bytes): preenchido com %3 bytes de 0xFF.")
                          .arg(originalSize).arg(chipSize).arg(padBytes));
        }
    }

    m_currentFilePath = path;
    m_hexEditor->setData(std::move(loadedData));

    const QFileInfo fi(path);
    m_bufferInfoLabel->setText(
        QStringLiteral("Arquivo [%1]: %2 (%3 bytes)")
            .arg(formatTag, fi.fileName())
            .arg(m_hexEditor->data().size()));
    updateBufferChecksums();

    appendLog(EzpWorker::LogLevel::Info,
              QStringLiteral("Arquivo %1 carregado: %2 (%3 bytes; buffer: %4 bytes).")
                  .arg(formatTag, path)
                  .arg(originalSize)
                  .arg(m_hexEditor->data().size()));
}

void MainWindow::onSaveFile()
{
    if (m_hexEditor->data().isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Buffer vazio"),
                                 QStringLiteral("Não há dados no buffer para salvar."));
        return;
    }

    const QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Salvar arquivo (.bin / Intel .hex)"),
        m_currentFilePath.isEmpty() ? QStringLiteral("dump.bin") : m_currentFilePath,
        QStringLiteral("Binário (*.bin *.rom);;Intel HEX (*.hex);;Todos os arquivos (*)"));

    if (path.isEmpty()) {
        return;
    }

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::critical(this, QStringLiteral("Erro ao salvar arquivo"), f.errorString());
        return;
    }

    const QByteArray payload = path.endsWith(QStringLiteral(".hex"), Qt::CaseInsensitive)
        ? BufferUtils::toIntelHex(m_hexEditor->data())
        : m_hexEditor->data();

    const qint64 written = f.write(payload);
    if (written != payload.size()) {
        QMessageBox::critical(this, QStringLiteral("Erro ao salvar arquivo"), f.errorString());
        return;
    }

    m_currentFilePath = path;
    m_hexEditor->setModified(false);
    updateBufferChecksums();
    appendLog(EzpWorker::LogLevel::Success,
              QStringLiteral("Buffer salvo em: %1 (%2 bytes).").arg(path).arg(written));
}

std::optional<std::size_t> MainWindow::selectedChipIndex() const
{
    if (m_selectedChip.has_value()) {
        return m_selectedChip->index;
    }
    return std::nullopt;
}

void MainWindow::onDetectChip()
{
    QMetaObject::invokeMethod(m_worker, &EzpWorker::detectChip, Qt::QueuedConnection);
}

void MainWindow::onReadChip()
{
    const auto chipIdx = selectedChipIndex();
    QMetaObject::invokeMethod(m_worker, [this, chipIdx]() {
        m_worker->readChip(chipIdx);
    }, Qt::QueuedConnection);
}

void MainWindow::onWriteChip()
{
    if (m_hexEditor->data().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Buffer vazio"),
                             QStringLiteral("Carregue um arquivo binário antes de gravar no chip."));
        return;
    }
    const QByteArray payload = m_hexEditor->data();
    const auto chipIdx = selectedChipIndex();
    QMetaObject::invokeMethod(m_worker, [this, payload, chipIdx]() {
        m_worker->writeChip(payload, chipIdx);
    }, Qt::QueuedConnection);
}

void MainWindow::onVerifyChip()
{
    if (m_hexEditor->data().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Buffer vazio"),
                             QStringLiteral("Carregue um arquivo binário para verificar contra o chip."));
        return;
    }
    const QByteArray payload = m_hexEditor->data();
    const auto chipIdx = selectedChipIndex();
    QMetaObject::invokeMethod(m_worker, [this, payload, chipIdx]() {
        m_worker->verifyChip(payload, chipIdx);
    }, Qt::QueuedConnection);
}

void MainWindow::onEraseChip()
{
    const auto answer = QMessageBox::question(
        this,
        QStringLiteral("Confirmar apagamento"),
        QStringLiteral("Tem certeza que deseja apagar todo o conteúdo do chip conectado?"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (answer != QMessageBox::Yes) {
        return;
    }

    const auto chipIdx = selectedChipIndex();
    QMetaObject::invokeMethod(m_worker, [this, chipIdx]() {
        m_worker->eraseChip(chipIdx);
    }, Qt::QueuedConnection);
}

void MainWindow::onAutoSequence()
{
    if (m_hexEditor->data().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Buffer vazio"),
                             QStringLiteral("Carregue um arquivo binário antes de iniciar a sequência Auto."));
        return;
    }

    const auto answer = QMessageBox::question(
        this,
        QStringLiteral("Confirmar sequência Auto"),
        QStringLiteral("A operação Auto irá:\n1. Apagar o chip\n2. Gravar o buffer atual\n3. Verificar os dados\n\nDeseja continuar?"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::Yes);

    if (answer != QMessageBox::Yes) {
        return;
    }

    const QByteArray payload = m_hexEditor->data();
    const auto chipIdx = selectedChipIndex();
    QMetaObject::invokeMethod(m_worker, [this, payload, chipIdx]() {
        m_worker->autoProgram(payload, chipIdx);
    }, Qt::QueuedConnection);
}

void MainWindow::onCancelOperation()
{
    if (m_worker != nullptr && m_isBusy) {
        m_worker->requestAbort();
        appendLog(EzpWorker::LogLevel::Warning, QStringLiteral("Cancelamento solicitado pelo usuário..."));
    }
}

void MainWindow::onUsbStateChanged(UsbConnectionState state)
{
    const bool changed = (!m_hasInitialUsbState || m_usbState != state);
    m_hasInitialUsbState = true;
    m_usbState = state;

    switch (state) {
    case UsbConnectionState::Connected:
        m_statusLed->setStyleSheet(QStringLiteral(
            "background-color: #22c55e; border: 1px solid #15803d; border-radius: 7px;"));
        m_statusText->setText(QStringLiteral("Conectado (USB 1fc8:310b)"));
        if (m_fixUdevBtn != nullptr) {
            m_fixUdevBtn->setVisible(false);
        }
        m_udevDialogShown = false;
        if (changed) {
            appendLog(EzpWorker::LogLevel::Success, QStringLiteral("Programador EZP2019 conectado e pronto."));
        }
        break;
    case UsbConnectionState::PermissionDenied:
        m_statusLed->setStyleSheet(QStringLiteral(
            "background-color: #f59e0b; border: 1px solid #b45309; border-radius: 7px;"));
        m_statusText->setText(QStringLiteral("Sem permissão USB (udev)"));
        if (m_fixUdevBtn != nullptr) {
            m_fixUdevBtn->setVisible(true);
        }
        if (changed) {
            appendLog(EzpWorker::LogLevel::Warning,
                      QStringLiteral("EZP2019 detectado com LIBUSB_ERROR_ACCESS. Instale a regra udev 99-ezp2019.rules."));
        }
        if (!m_udevDialogShown && isVisible()) {
            m_udevDialogShown = true;
            showUsbPermissionDialog();
        }
        break;
    case UsbConnectionState::Disconnected:
        m_statusLed->setStyleSheet(QStringLiteral(
            "background-color: #ef4444; border: 1px solid #991b1b; border-radius: 7px;"));
        m_statusText->setText(QStringLiteral("Desconectado"));
        if (m_fixUdevBtn != nullptr) {
            m_fixUdevBtn->setVisible(false);
        }
        if (changed && m_logView != nullptr) {
            appendLog(EzpWorker::LogLevel::Warning, QStringLiteral("Programador EZP2019 desconectado."));
        }
        break;
    }

    updateActionsState();
}

void MainWindow::showUsbPermissionDialog()
{
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(QStringLiteral("Permissão USB necessária (LIBUSB_ERROR_ACCESS)"));
    box.setText(QStringLiteral(
        "O programador EZP2019 (VID 1fc8 / PID 310b) foi detectado na porta USB, mas seu usuário não tem permissão de acesso."));
    box.setInformativeText(QStringLiteral(
        "No Linux, o acesso direto a dispositivos USB por usuários comuns requer uma regra udev em "
        "/etc/udev/rules.d/99-ezp2019.rules.\n\n"
        "Clique em \"Instalar regra udev\" para configurar automaticamente via pkexec (sem precisar rodar a GUI como root)."));

    QPushButton *installBtn = box.addButton(QStringLiteral("Instalar regra udev (pkexec)"), QMessageBox::AcceptRole);
    box.addButton(QStringLiteral("Mais tarde"), QMessageBox::RejectRole);
    box.exec();

    if (box.clickedButton() == installBtn) {
        onInstallUdevRule();
    }
}

void MainWindow::onInstallUdevRule()
{
    const QString script = QStringLiteral(
        "cat <<'EOF' > /etc/udev/rules.d/99-ezp2019.rules\n"
        "# EZP2019 High-Speed USB Programmer (VID: 0x1fc8, PID: 0x310b)\n"
        "SUBSYSTEM==\"usb\", ATTR{idVendor}==\"1fc8\", ATTR{idProduct}==\"310b\", MODE=\"0666\", TAG+=\"uaccess\"\n"
        "SUBSYSTEM==\"usb_device\", ATTR{idVendor}==\"1fc8\", ATTR{idProduct}==\"310b\", MODE=\"0666\", TAG+=\"uaccess\"\n"
        "EOF\n"
        "udevadm control --reload-rules && "
        "udevadm trigger --attr-match=idVendor=1fc8 --attr-match=idProduct=310b");

    appendLog(EzpWorker::LogLevel::Info,
              QStringLiteral("Solicitando privilégios via pkexec para instalar /etc/udev/rules.d/99-ezp2019.rules..."));

    QProcess proc;
    proc.start(QStringLiteral("pkexec"), {QStringLiteral("sh"), QStringLiteral("-c"), script});
    if (!proc.waitForStarted(5000)) {
        QMessageBox::critical(
            this,
            QStringLiteral("pkexec não encontrado"),
            QStringLiteral(
                "Não foi possível executar o pkexec.\n\n"
                "Instale a regra manualmente executando no terminal:\n"
                "sudo install -Dm644 99-ezp2019.rules /etc/udev/rules.d/99-ezp2019.rules && "
                "sudo udevadm control --reload-rules && sudo udevadm trigger"));
        return;
    }

    proc.waitForFinished(-1);
    if (proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0) {
        appendLog(EzpWorker::LogLevel::Success,
                  QStringLiteral("Regra udev 99-ezp2019.rules instalada e recarregada com sucesso."));
        QMessageBox::information(
            this,
            QStringLiteral("Regra udev instalada"),
            QStringLiteral(
                "A regra /etc/udev/rules.d/99-ezp2019.rules foi instalada e aplicada.\n\n"
                "Se o indicador não ficar verde automaticamente em alguns segundos, desconecte e reconecte o cabo USB do EZP2019."));
    } else {
        const QString errOut = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        appendLog(EzpWorker::LogLevel::Warning,
                  QStringLiteral("Instalação da regra udev cancelada ou falhou (código %1): %2")
                      .arg(proc.exitCode())
                      .arg(errOut.isEmpty() ? QStringLiteral("cancelado pelo usuário") : errOut));
    }
}

void MainWindow::onOperationStarted(const QString &operationName)
{
    m_isBusy = true;
    if (m_usbMonitor != nullptr) {
        m_usbMonitor->setPaused(true);
    }
    updateActionsState();
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setFormat(QStringLiteral("%p%"));
    m_progressLabel->setText(operationName);
}

void MainWindow::onOperationFinished(bool success, const QString &summary)
{
    m_isBusy = false;
    if (m_usbMonitor != nullptr) {
        m_usbMonitor->setPaused(false);
    }
    updateActionsState();
    m_progressLabel->setText(summary);
    if (!success) {
        m_progressBar->setValue(0);
        m_progressBar->setFormat(QStringLiteral("%p%"));
    }
}

void MainWindow::onProgressChanged(qint64 processed, qint64 total)
{
    if (total <= 0) {
        return;
    }
    const int pct = static_cast<int>((processed * 100) / total);
    m_progressBar->setValue(qBound(0, pct, 100));
    if (total > 100) {
        m_progressBar->setFormat(
            QStringLiteral("%p% (%1 / %2 KB)")
                .arg(static_cast<double>(processed) / 1024.0, 0, 'f', 1)
                .arg(static_cast<double>(total) / 1024.0, 0, 'f', 1));
    } else {
        m_progressBar->setFormat(QStringLiteral("%p%"));
    }
}

void MainWindow::onLogMessage(EzpWorker::LogLevel level, const QString &message)
{
    appendLog(level, message);
}

void MainWindow::onChipDetected(uint32_t chipId)
{
    const QString idHex = QStringLiteral("0x%1").arg(chipId, 6, 16, QLatin1Char('0')).toUpper();

    // 0x000000 or 0xFFFFFF almost always means no chip, bad ZIF contact, inverted Pin 1,
    // or a 24xx/93xx EEPROM that does not support JEDEC Auto-Detect.
    if (chipId == 0x000000U || chipId == 0xFFFFFFU || chipId == EXP2019_INVALID_CHIP_ID) {
        appendLog(EzpWorker::LogLevel::Warning,
                  QStringLiteral("Nenhum chip válido detectado (ID lido: %1).").arg(idHex));
        QMessageBox::warning(
            this,
            QStringLiteral("Chip não detectado"),
            QStringLiteral(
                "Não foi possível identificar nenhum chip conectado (ID lido: %1).\n\n"
                "Verifique:\n"
                "• Se o chip está bem encaixado e travado pela alavanca do soquete ZIF;\n"
                "• Se o Pino 1 está orientado corretamente (conforme o diagrama lateral);\n"
                "• Se o chip é uma EEPROM 24xx/93xx (esses modelos não possuem JEDEC ID e devem ser selecionados manualmente).")
                .arg(idHex));
        return;
    }

    const auto matches = m_chipDb.findAllByChipId(chipId);
    if (!matches.isEmpty()) {
        const ChipInfo &primary = matches.first();
        selectChip(primary);
        appendLog(EzpWorker::LogLevel::Success,
                  QStringLiteral("Chip detectado: %1 %2 (%3) — %4 [ID: %5]")
                      .arg(primary.manufacturer, primary.name, primary.displayType, primary.formattedCapacity(), idHex));

        if (matches.size() > 1) {
            QStringList alts;
            for (qsizetype i = 1; i < matches.size(); ++i) {
                alts.push_back(QStringLiteral("%1 %2").arg(matches[i].manufacturer, matches[i].name));
            }
            appendLog(EzpWorker::LogLevel::Info,
                      QStringLiteral("Nota: %1 modelos compartilham o mesmo JEDEC ID (%2). Alternativas: %3.")
                          .arg(matches.size())
                          .arg(idHex, alts.join(QStringLiteral(", "))));
        }
    } else {
        appendLog(EzpWorker::LogLevel::Warning,
                  QStringLiteral("Chip desconhecido na base (JEDEC ID: %1).").arg(idHex));
        QMessageBox::warning(
            this,
            QStringLiteral("Chip não reconhecido"),
            QStringLiteral(
                "O programador respondeu com o JEDEC ID %1, mas este código não consta na base de chips suportados.\n\n"
                "Verifique o encaixe no soquete ZIF ou selecione manualmente um modelo equivalente nos menus.")
                .arg(idHex));
    }
}

void MainWindow::onReadCompleted(const QByteArray &data)
{
    m_currentFilePath.clear();
    m_hexEditor->setData(data);
    m_bufferInfoLabel->setText(
        QStringLiteral("Buffer: lido do chip (%1 bytes)").arg(data.size()));
    updateBufferChecksums();

    appendLog(EzpWorker::LogLevel::Info,
              QStringLiteral("Checksums da leitura — CRC32: %1 | MD5: %2")
                  .arg(BufferUtils::computeCrc32Hex(data), BufferUtils::computeMd5Hex(data)));
}

void MainWindow::onFillBufferFF()
{
    if (m_hexEditor->data().isEmpty()) {
        if (m_selectedChip.has_value() && m_selectedChip->size > 0) {
            m_hexEditor->fill(0xFF, static_cast<qsizetype>(m_selectedChip->size));
        } else {
            return;
        }
    } else {
        m_hexEditor->fillExisting(0xFF);
    }

    m_bufferInfoLabel->setText(
        QStringLiteral("Buffer: preenchido com 0xFF (%1 bytes)").arg(m_hexEditor->data().size()));
    updateBufferChecksums();
    appendLog(EzpWorker::LogLevel::Info,
              QStringLiteral("Buffer preenchido com 0xFF (%1 bytes).").arg(m_hexEditor->data().size()));
}

void MainWindow::onFillBuffer00()
{
    if (m_hexEditor->data().isEmpty()) {
        if (m_selectedChip.has_value() && m_selectedChip->size > 0) {
            m_hexEditor->fill(0x00, static_cast<qsizetype>(m_selectedChip->size));
        } else {
            return;
        }
    } else {
        m_hexEditor->fillExisting(0x00);
    }

    m_bufferInfoLabel->setText(
        QStringLiteral("Buffer: preenchido com 0x00 (%1 bytes)").arg(m_hexEditor->data().size()));
    updateBufferChecksums();
    appendLog(EzpWorker::LogLevel::Info,
              QStringLiteral("Buffer preenchido com 0x00 (%1 bytes).").arg(m_hexEditor->data().size()));
}

bool MainWindow::buildSearchPattern(QByteArray &pattern) const
{
    const QString query = m_hexSearchEdit->text();
    if (query.isEmpty()) {
        return false;
    }

    if (m_hexSearchMode->currentIndex() == 0) {
        if (!BufferUtils::parseHexPattern(query, pattern)) {
            QMessageBox::warning(
                const_cast<MainWindow *>(this),
                QStringLiteral("Sequência hexadecimal inválida"),
                QStringLiteral("Informe um número par de dígitos hexadecimais (ex.: EF 40 17 ou DEADBEEF)."));
            return false;
        }
    } else {
        pattern = query.toUtf8();
    }
    return !pattern.isEmpty();
}

void MainWindow::onFindNext()
{
    QByteArray pattern;
    if (!buildSearchPattern(pattern)) {
        return;
    }

    const qsizetype found = m_hexEditor->findNext(pattern);
    if (found < 0) {
        appendLog(EzpWorker::LogLevel::Warning,
                  QStringLiteral("Sequência \"%1\" não encontrada no buffer.").arg(m_hexSearchEdit->text()));
        statusBar()->showMessage(QStringLiteral("Sequência não encontrada."), 3000);
    } else {
        statusBar()->showMessage(
            QStringLiteral("Encontrado em 0x%1 (%2)")
                .arg(found, 8, 16, QLatin1Char('0')).toUpper()
                .arg(found),
            3000);
    }
}

void MainWindow::onFindPrevious()
{
    QByteArray pattern;
    if (!buildSearchPattern(pattern)) {
        return;
    }

    const qsizetype found = m_hexEditor->findPrevious(pattern);
    if (found < 0) {
        appendLog(EzpWorker::LogLevel::Warning,
                  QStringLiteral("Sequência \"%1\" não encontrada no buffer.").arg(m_hexSearchEdit->text()));
        statusBar()->showMessage(QStringLiteral("Sequência não encontrada."), 3000);
    } else {
        statusBar()->showMessage(
            QStringLiteral("Encontrado em 0x%1 (%2)")
                .arg(found, 8, 16, QLatin1Char('0')).toUpper()
                .arg(found),
            3000);
    }
}

void MainWindow::onBufferDataModified(qsizetype, uint8_t)
{
    if (m_statusModLabel != nullptr) {
        m_statusModLabel->setText(QStringLiteral("Modificados: %1").arg(m_hexEditor->modifiedCount()));
    }
    if (m_checksumDebounceTimer != nullptr) {
        m_checksumDebounceTimer->start();
    }
}

void MainWindow::updateBufferChecksums()
{
    if (m_statusCrcLabel == nullptr || m_statusMd5Label == nullptr) {
        return;
    }

    const QByteArray &buf = m_hexEditor->data();
    if (buf.isEmpty()) {
        m_statusCrcLabel->setText(QStringLiteral("CRC32: --------"));
        m_statusMd5Label->setText(QStringLiteral("MD5: --------------------------------"));
        m_statusModLabel->setText(QStringLiteral("Modificados: 0"));
        return;
    }

    m_statusCrcLabel->setText(QStringLiteral("CRC32: %1").arg(BufferUtils::computeCrc32Hex(buf)));
    m_statusMd5Label->setText(QStringLiteral("MD5: %1").arg(BufferUtils::computeMd5Hex(buf)));
    m_statusModLabel->setText(QStringLiteral("Modificados: %1").arg(m_hexEditor->modifiedCount()));
}

void MainWindow::onCursorChanged(qsizetype offset, bool hasData, uint8_t value)
{
    if (!hasData) {
        m_cursorInfoLabel->setText(QStringLiteral("Cursor: -"));
        return;
    }

    const QChar ascii = (value >= 0x20 && value <= 0x7E) ? QChar(value) : QChar(u'.');
    m_cursorInfoLabel->setText(
        QStringLiteral("Offset: 0x%1 (%2) | Byte: 0x%3 (%4) '%5'")
            .arg(offset, 8, 16, QLatin1Char('0')).toUpper()
            .arg(offset)
            .arg(value, 2, 16, QLatin1Char('0')).toUpper()
            .arg(value)
            .arg(ascii));
}

void MainWindow::updateActionsState()
{
    const bool fileActionsEnabled = !m_isBusy;
    const bool hwActionsEnabled = !m_isBusy && (m_usbState == UsbConnectionState::Connected);

    m_actOpen->setEnabled(fileActionsEnabled);
    m_actSave->setEnabled(fileActionsEnabled);
    m_actRead->setEnabled(hwActionsEnabled);
    m_actWrite->setEnabled(hwActionsEnabled);
    m_actVerify->setEnabled(hwActionsEnabled);
    m_actErase->setEnabled(hwActionsEnabled);
    m_actDetect->setEnabled(hwActionsEnabled);
    m_actAuto->setEnabled(hwActionsEnabled);
    m_actCancel->setEnabled(m_isBusy);
}

void MainWindow::appendLog(EzpWorker::LogLevel level, const QString &message)
{
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    QString tag;
    QString color;

    switch (level) {
    case EzpWorker::LogLevel::Info:
        tag = QStringLiteral("INFO");
        color = QStringLiteral("#64748b");
        break;
    case EzpWorker::LogLevel::Success:
        tag = QStringLiteral("OK  ");
        color = QStringLiteral("#16a34a");
        break;
    case EzpWorker::LogLevel::Warning:
        tag = QStringLiteral("WARN");
        color = QStringLiteral("#d97706");
        break;
    case EzpWorker::LogLevel::Error:
        tag = QStringLiteral("ERR ");
        color = QStringLiteral("#dc2626");
        break;
    }

    const QString html = QStringLiteral(
        "<span style=\"color:#94a3b8;\">[%1]</span> "
        "<span style=\"color:%2; font-weight:bold;\">[%3]</span> %4")
                             .arg(ts, color, tag, message.toHtmlEscaped());
    m_logView->appendHtml(html);
}
