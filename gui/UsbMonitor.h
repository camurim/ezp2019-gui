#pragma once

#include "ezp2019.h"

#include <QObject>
#include <QThread>

#include <atomic>

enum class UsbConnectionState {
    Disconnected,
    Connected,
    PermissionDenied
};
Q_DECLARE_METATYPE(UsbConnectionState)

class UsbMonitor : public QObject {
    Q_OBJECT

public:
    explicit UsbMonitor(int intervalMs = 1500, QObject *parent = nullptr);
    ~UsbMonitor() override;

    void start();
    void stop();

    // Pause polling while an I/O worker owns the USB interface
    void setPaused(bool paused);

signals:
    void connectionStateChanged(UsbConnectionState state);

private:
    void pollLoop();

    int m_intervalMs = 1500;
    QThread *m_thread = nullptr;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_paused{false};
};
