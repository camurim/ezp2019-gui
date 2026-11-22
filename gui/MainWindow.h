#pragma once

#include "ChipDatabase.h"
#include "EzpWorker.h"
#include "UsbMonitor.h"

#include <QMainWindow>
#include <QThread>

#include <optional>

class QAction;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTimer;
class HexEditorWidget;
class ZifSocketWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    // File actions
    void onOpenFile();
    void onSaveFile();

    // Hardware actions
    void onReadChip();
    void onWriteChip();
    void onVerifyChip();
    void onEraseChip();
    void onDetectChip();
    void onAutoSequence();
    void onCancelOperation();

    // Chip selection cascading slots
    void onMemoryTypeChanged(int index);
    void onManufacturerChanged(int index);
    void onChipModelChanged(int index);
    void onPackageChanged(int index);
    void onQuickSearchActivated(const QString &text);

    // Worker & USB monitor feedback slots
    void onUsbStateChanged(UsbConnectionState state);
    void onInstallUdevRule();
    void onOperationStarted(const QString &operationName);
    void onOperationFinished(bool success, const QString &summary);
    void onProgressChanged(qint64 processed, qint64 total);
    void onLogMessage(EzpWorker::LogLevel level, const QString &message);
    void onChipDetected(uint32_t chipId);
    void onReadCompleted(const QByteArray &data);

    // Hex editor & buffer actions
    void onFillBufferFF();
    void onFillBuffer00();
    void onFindNext();
    void onFindPrevious();
    void onCursorChanged(qsizetype offset, bool hasData, uint8_t value);
    void onBufferDataModified(qsizetype offset, uint8_t newValue);

private:
    void setupToolbar();
    void setupUi();
    void setupStatusBar();
    void setupWorker();
    void populateMemoryTypes();
    void selectChip(const ChipInfo &chip);
    void updateChipSpecLabels(const std::optional<ChipInfo> &chip);
    void updateBufferChecksums();
    void updateActionsState();
    std::optional<std::size_t> selectedChipIndex() const;
    bool buildSearchPattern(QByteArray &pattern) const;
    void showUsbPermissionDialog();
    void appendLog(EzpWorker::LogLevel level, const QString &message);

    ChipDatabase m_chipDb;
    std::optional<ChipInfo> m_selectedChip;
    QString m_currentFilePath;
    UsbConnectionState m_usbState = UsbConnectionState::Disconnected;
    bool m_hasInitialUsbState = false;
    bool m_udevDialogShown = false;
    bool m_isBusy = false;

    // Toolbar actions
    QAction *m_actOpen = nullptr;
    QAction *m_actSave = nullptr;
    QAction *m_actRead = nullptr;
    QAction *m_actWrite = nullptr;
    QAction *m_actVerify = nullptr;
    QAction *m_actErase = nullptr;
    QAction *m_actDetect = nullptr;
    QAction *m_actAuto = nullptr;
    QAction *m_actCancel = nullptr;

    // Left panel widgets
    QLabel *m_statusLed = nullptr;
    QLabel *m_statusText = nullptr;
    QPushButton *m_fixUdevBtn = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QComboBox *m_typeCombo = nullptr;
    QComboBox *m_manufCombo = nullptr;
    QComboBox *m_chipCombo = nullptr;
    QLabel *m_specSize = nullptr;
    QLabel *m_specPage = nullptr;
    QLabel *m_specVoltage = nullptr;
    QLabel *m_specId = nullptr;
    QLabel *m_specProtocol = nullptr;
    QComboBox *m_packageCombo = nullptr;
    ZifSocketWidget *m_zifWidget = nullptr;

    // Central widgets
    QComboBox *m_hexSearchMode = nullptr;
    QLineEdit *m_hexSearchEdit = nullptr;
    QLabel *m_bufferInfoLabel = nullptr;
    QLabel *m_cursorInfoLabel = nullptr;
    HexEditorWidget *m_hexEditor = nullptr;

    // Status bar checksum labels
    QLabel *m_statusCrcLabel = nullptr;
    QLabel *m_statusMd5Label = nullptr;
    QLabel *m_statusModLabel = nullptr;
    QTimer *m_checksumDebounceTimer = nullptr;

    // Bottom widgets
    QProgressBar *m_progressBar = nullptr;
    QLabel *m_progressLabel = nullptr;
    QPlainTextEdit *m_logView = nullptr;

    // Background worker & dedicated USB monitor thread
    QThread m_workerThread;
    EzpWorker *m_worker = nullptr;
    UsbMonitor *m_usbMonitor = nullptr;
};
