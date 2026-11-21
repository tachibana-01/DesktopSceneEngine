#pragma once

#include "SessionApplication.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <utility>

class SessionApplicationTestAccess
{
public:
    static DesktopSceneEngine::Player::WorkspaceModel& model(
        DesktopSceneEngine::Player::SessionApplication& application)
    {
        return application.workspaceModel_;
    }
    static DesktopSceneEngine::Player::CanvasOutsidePreferenceRead persistedBackground(
        const DesktopSceneEngine::Player::SessionApplication& application)
    {
        return application.persistedCanvasOutsideMode();
    }
    static void displayEnumerator(DesktopSceneEngine::Player::SessionApplication& application,
        std::function<DesktopSceneEngine::Platform::DisplayEnumerationResult()> enumerate)
    {
        application.displayEnumeratorForTesting_ = std::move(enumerate);
    }
};

namespace DesktopSceneEngine::Player::TestSupport
{

[[nodiscard]] inline bool awaitAcceptedSessionClose(SessionApplication& application)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (application.closeState() != SessionApplication::CloseState::Accepted)
    {
        if (application.closeState() == SessionApplication::CloseState::Open) return false;
        application.pollNativeAudioEffects();
        QCoreApplication::processEvents();
        if (elapsed.elapsed() > 5000) return false;
        QThread::msleep(1);
    }
    return !application.engineInitialized() && !application.presentationEngine().isInitialized()
        && application.presentationEngine().audioShutdownComplete();
}

}
