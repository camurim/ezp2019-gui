#include "UsbMonitor.h"

#include <QMetaObject>

UsbMonitor::UsbMonitor(int intervalMs, QObject *parent)
    : QObject(parent)
    , m_intervalMs(intervalMs)
{
    qRegisterMetaType<UsbConnectionState>("UsbConnectionState");
}

UsbMonitor::~UsbMonitor()
{
    stop();
}

void UsbMonitor::start()
{
    if (m_running.exchange(true)) {
        return;
    }

    m_thread = QThread::create([this]() { pollLoop(); });
    m_thread->setObjectName(QStringLiteral("ezp2019-usb-monitor"));
    m_thread->start();
}

void UsbMonitor::stop()
{
    if (!m_running.exchange(false)) {
        return;
    }

    if (m_thread != nullptr) {
        m_thread->wait(3000);
        delete m_thread;
        m_thread = nullptr;
    }
}

void UsbMonitor::setPaused(bool paused)
{
    m_paused.store(paused, std::memory_order_relaxed);
}

void UsbMonitor::pollLoop()
{
    exp2019 handle = nullptr;
    if (exp2019_init(&handle) != EXP2019_NO_ERROR) {
        return;
    }

    bool firstEmit = true;
    UsbConnectionState lastState = UsbConnectionState::Disconnected;

    while (m_running.load(std::memory_order_relaxed)) {
        if (!m_paused.load(std::memory_order_relaxed)) {
            bool connected = false;
            bool permDenied = false;
            const int res = exp2019_connection_status(handle, &connected, &permDenied);

            UsbConnectionState state = UsbConnectionState::Disconnected;
            if (res == EXP2019_NO_ERROR && connected) {
                state = permDenied ? UsbConnectionState::PermissionDenied
                                   : UsbConnectionState::Connected;
            }

            if (firstEmit || state != lastState) {
                firstEmit = false;
                lastState = state;
                emit connectionStateChanged(state);
            }
        }

        // Sleep in small slices so stop() responds promptly
        constexpr int kSliceMs = 100;
        int remaining = m_intervalMs;
        while (remaining > 0 && m_running.load(std::memory_order_relaxed)) {
            QThread::msleep(static_cast<unsigned long>(remaining < kSliceMs ? remaining : kSliceMs));
            remaining -= kSliceMs;
        }
    }

    exp2019_exit(handle);
}
