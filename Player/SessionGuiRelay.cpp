#include "SessionGuiRelay.h"

#include <QDebug>
#include <QScopedValueRollback>
#include <utility>

namespace DesktopSceneEngine::Player
{
SessionGuiRelay::SessionGuiRelay()
{
    QObject::connect(&pump_, &QTimer::timeout, &pump_, [this] { drain(); });
    pump_.setTimerType(Qt::PreciseTimer);
    pump_.start(8);
}

bool SessionGuiRelay::post(Delivery delivery)
{
    const auto write = writeIndex_.load(std::memory_order_relaxed);
    const auto next = (write + 1) % Capacity;
    if (next == readIndex_.load(std::memory_order_acquire))
    {
        overflowed_.store(true, std::memory_order_release);
        return false;
    }
    queue_[write] = std::move(delivery);
    writeIndex_.store(next, std::memory_order_release);
    return true;
}

void SessionGuiRelay::drain()
{
    if (draining_) return;
    const QScopedValueRollback guard(draining_, true);
    if (overflowed_.exchange(false, std::memory_order_acq_rel))
        qCritical("Session GUI reply relay exceeded its admitted operation capacity");
    // Work newly posted during this turn stays for the next turn.
    auto read = readIndex_.load(std::memory_order_relaxed);
    const auto end = writeIndex_.load(std::memory_order_acquire);
    while (read != end)
    {
        auto delivery = std::exchange(queue_[read], Delivery{});
        read = (read + 1) % Capacity;
        readIndex_.store(read, std::memory_order_release);
        if (delivery) delivery();
    }
}
}
