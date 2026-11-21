#include "SessionControlExecutor.h"
#include "SessionApplication.h"
#include <DesktopSceneEngine/Engine.h>
#include <QtCore/qt_windows.h>
#include <QWinEventNotifier>
#include <atomic>
#include <stdexcept>
#include <utility>

namespace DesktopSceneEngine::Player
{
namespace
{
constexpr std::size_t MaximumQueuedCommands = 128;
}

class SessionControlExecutor::Endpoint final : public QObject
{
public:
    explicit Endpoint(HANDLE audioFactEvent) : audioFactEvent_(audioFactEvent) {}

    bool start(DisplayId display, ApplicationPreferences initialPreferences,
        const SettingsFactory& openSettings, const PlatformFactory& makePlatform)
    {
        if (openSettings)
        {
            settings_ = openSettings();
            if (!settings_) return false;
            preferences_ = std::make_unique<ApplicationPreferencesStore>(*settings_);
        }
        engine_ = std::make_unique<Engine>(Engine::PresentationMode::Detached);
        session_ = std::make_unique<SessionApplication>(*engine_, std::move(display), preferences_.get(), initialPreferences);
        if (!session_->initializeAutonomousRuntime(audioFactEvent_))
        {
            retire();
            return false;
        }
        if (makePlatform)
        {
            platform_ = makePlatform(*session_);
            if (!platform_) return false;
            session_->attachPlatform(*platform_);
        }
        audioFacts_ = std::make_unique<QWinEventNotifier>(audioFactEvent_);
        QObject::connect(audioFacts_.get(), &QWinEventNotifier::activated, this, [this] {
            session_->pollNativeAudioEffects();
        });
        return true;
    }

    bool enqueue(Command command, bool capacityBound)
    {
        if (capacityBound && queued_.load(std::memory_order_relaxed) >= MaximumQueuedCommands) return false;
        if (capacityBound) queued_.fetch_add(1, std::memory_order_relaxed);
        if (QMetaObject::invokeMethod(this, [this, command = std::move(command), capacityBound] {
            if (capacityBound) queued_.fetch_sub(1, std::memory_order_relaxed);
            if (session_) command(*session_);
        }, Qt::QueuedConnection)) return true;
        if (capacityBound) queued_.fetch_sub(1, std::memory_order_relaxed);
        return false;
    }

    void retire()
    {
        audioFacts_.reset();
        if (session_ && platform_) session_->detachPlatform(*platform_);
        session_.reset();
        platform_.reset();
        engine_.reset();
        preferences_.reset();
        settings_.reset();
    }

private:
    const HANDLE audioFactEvent_;
    std::atomic<std::size_t> queued_{0};
    std::unique_ptr<QSettings> settings_;
    std::unique_ptr<ApplicationPreferencesStore> preferences_;
    std::unique_ptr<Engine> engine_;
    std::unique_ptr<SessionApplication> session_;
    std::unique_ptr<SessionPlatform> platform_;
    std::unique_ptr<QWinEventNotifier> audioFacts_;
};

SessionControlExecutor::SessionControlExecutor()
{
    audioFactEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!audioFactEvent_) throw std::runtime_error("Unable to create Session control fact event");
}

SessionControlExecutor::~SessionControlExecutor()
{
    stop();
    thread_.wait();
    CloseHandle(audioFactEvent_);
}

void SessionControlExecutor::start(DisplayId display, ApplicationPreferences initialPreferences,
    SettingsFactory openSettings, Started started, PlatformFactory platform)
{
    const std::lock_guard lock(admission_);
    if (started_) throw std::logic_error("The Session control executor can start only once");
    endpoint_ = new Endpoint(audioFactEvent_);
    endpoint_->moveToThread(&thread_);
    QObject::connect(&thread_, &QThread::finished, endpoint_, &QObject::deleteLater);
    started_ = true;
    thread_.setObjectName(QStringLiteral("Session control"));
    thread_.start();
    QMetaObject::invokeMethod(endpoint_, [this, display = std::move(display), initialPreferences,
        openSettings = std::move(openSettings), started = std::move(started), platform = std::move(platform)]() mutable {
        bool ready = false;
        try
        {
            ready = endpoint_->start(std::move(display), initialPreferences, openSettings, platform);
            if (!ready) endpoint_->retire();
        }
        catch (const std::exception& e)
        {
            qCritical("Session control startup failed: %s", e.what());
            endpoint_->retire();
        }
        {
            const std::lock_guard lock(admission_);
            accepting_ = ready && !stopping_;
            ready = accepting_;
        }
        if (started) started(ready);
    }, Qt::QueuedConnection);
}

bool SessionControlExecutor::submit(Command command)
{
    if (!command) return false;
    const std::lock_guard lock(admission_);
    return accepting_ && endpoint_->enqueue(std::move(command), true);
}

bool SessionControlExecutor::submitPlatformResult(Command result)
{
    if (!result) return false;
    const std::lock_guard lock(admission_);
    return accepting_ && endpoint_->enqueue(std::move(result), false);
}

void SessionControlExecutor::stop()
{
    const std::lock_guard lock(admission_);
    if (!endpoint_ || stopping_) return;
    stopping_ = true;
    accepting_ = false;
    QMetaObject::invokeMethod(endpoint_, [endpoint = endpoint_] {
        endpoint->retire();
        QThread::currentThread()->quit();
    }, Qt::QueuedConnection);
}
}
