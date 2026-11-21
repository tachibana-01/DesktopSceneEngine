// Direct composition test for the current CP4 shell and autonomous Session.
// The executable admission and package-failure paths are qualified separately.
#include "ModelCSessionWindow.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>
#include <stdexcept>

using namespace DesktopSceneEngine;
using namespace DesktopSceneEngine::Player;

namespace
{
template <class Predicate>
bool waitFor(Predicate ready)
{
    QElapsedTimer timer;
    timer.start();
    do
    {
        QApplication::processEvents();
        if (ready()) return true;
        QThread::msleep(10);
    } while (timer.elapsed() < 20000);
    return false;
}
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);

    QTemporaryDir profile;
    if (!profile.isValid()) return 1;
    if (!qputenv("WEBVIEW2_USER_DATA_FOLDER", profile.path().toUtf8())) return 1;

    try
    {
        ApplicationTranslationState translation;
        SessionGuiRelay guiRelay;
        SessionControlExecutor control;
        const auto display = DisplayId::fromTargetDevicePath("CP5-Model-C-Production-Test");
        ModelCSessionWindow window{control, guiRelay, display, translation, QUuid::createUuid()};
        window.setAttribute(Qt::WA_ShowWithoutActivating);
        bool controlStarted = false;
        bool controlSucceeded = false;
        bool closeAccepted = false;
        const auto stop = qScopeGuard([&] {
            window.releasePresentation();
            control.stop();
        });

        window.setAcceptedCloseCallback([&] { closeAccepted = true; });
        control.start(display, {}, {}, [&](bool ready) {
            (void)guiRelay.post([&, ready] {
                controlStarted = true;
                controlSucceeded = ready;
                if (ready) window.startPresentation();
            });
        }, window.platformFactory());

        window.show();
        if (!waitFor([&] { return (controlStarted && !controlSucceeded)
                || window.productFrameFailed() || window.isProductPresentationAvailable(); })
            || !controlSucceeded || window.productFrameFailed() || !window.isVisible()
            || !window.isProductPresentationAvailable())
            throw std::runtime_error("Model C modern Session did not reach useful production readiness");

        window.close();
        if (!waitFor([&] { return closeAccepted && !window.isVisible(); }))
            throw std::runtime_error("Modern Session did not finish ordinary Close");

        std::cout << "Current Model C Session reached product readiness and accepted ordinary Close\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
