#pragma once

#include <QTimer>
#include <array>
#include <atomic>
#include <functional>

namespace DesktopSceneEngine::Player
{
// A single control producer and single GUI consumer. GUI drains the relay
// from its own loop; control never locks Qt's GUI event queue to send a reply.
// Web commands reserve at most 96 completion slots, while frame/sample lanes
// coalesce before posting and native platform operations have finite admissions.
class SessionGuiRelay final
{
public:
    using Delivery = std::function<void()>;
    SessionGuiRelay();
    SessionGuiRelay(const SessionGuiRelay&) = delete;
    SessionGuiRelay& operator=(const SessionGuiRelay&) = delete;
    bool post(Delivery delivery);

private:
    void drain();
    static constexpr std::size_t Capacity = 256;
    std::array<Delivery, Capacity> queue_;
    std::atomic_size_t readIndex_{0};
    std::atomic_size_t writeIndex_{0};
    std::atomic_bool overflowed_{false};
    bool draining_ = false;
    QTimer pump_;
};
}
