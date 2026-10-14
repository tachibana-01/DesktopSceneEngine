#include "ApplicationPreferences.h"
#include "LauncherWindow.h"
#include "MainWindow.h"

#include <QAccessible>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFontMetrics>
#include <QFontDatabase>
#include <QEnterEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>
#include <QRawFont>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolButton>
#include <windows.h>

#include <iostream>
#include <stdexcept>

namespace
{
using namespace DesktopSceneEngine::Player;
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

void inspect(QMainWindow& window, SessionPresentation& presentation, const char* name,
    const QString& directory, bool survey)
{
    auto* button = window.findChild<QToolButton*>(name);
    require(button, "S8 actual production client Hide control exists");
    bool qualified = true;
    // UI-only qualification seam: no fake role/admission/Engine or Human Gate.
    presentation.bindRecovery([&] { return qualified; }, [] {});
    window.show(); QApplication::processEvents();
    const auto report = [&](const QString& state) {
        const auto ink = QFontMetrics(button->font()).tightBoundingRect(button->text());
        const auto point = button->mapTo(window.centralWidget(), QPoint{});
        const auto* accessible = QAccessible::queryAccessibleInterface(button);
        std::cout << QJsonDocument(QJsonObject{
            {"control", name}, {"state", state}, {"font", button->font().family()},
            {"pixelSize", button->font().pixelSize()}, {"pointSize", button->font().pointSizeF()},
            {"inkWidth", ink.width()}, {"inkHeight", ink.height()},
            {"width", button->width()}, {"height", button->height()}, {"x", point.x()}, {"y", point.y()},
            {"rightGap", window.centralWidget()->width() - point.x() - button->width()},
            {"dpr", window.devicePixelRatioF()}, {"logicalDpi", window.screen()->logicalDotsPerInch()},
            {"enabled", button->isEnabled()}, {"autoRaise", button->autoRaise()},
            {"tooltip", button->toolTip()}, {"accessibleName", accessible ? accessible->text(QAccessible::Name) : QString{}}
        }).toJson(QJsonDocument::Compact).constData() << std::endl;
        if (!directory.isEmpty()) {
            require(QDir{}.mkpath(directory), "S8 capture directory");
            require(button->grab().save(directory + '/' + name + '-' + state + ".png"), "S8 actual Qt control snapshot");
        }
    };
    report("baseline");
    if (survey) {
        for (const int pixels : {16, 18, 20}) {
            auto font = button->font(); font.setPixelSize(pixels); button->setFont(font);
            QApplication::processEvents(); report(QString::number(pixels));
        }
        for (const auto* family : {"Segoe UI", "Segoe UI Symbol"}) {
            auto font = button->font(); font.setFamily(family); font.setPixelSize(18); button->setFont(font);
            QApplication::processEvents(); report(QString(family).replace(' ', '_'));
        }
        for (const auto* family : {"Segoe Fluent Icons", "Segoe MDL2 Assets"}) {
            if (!QFontDatabase::families().contains(family)) continue;
            button->setText(QString(QChar(0xE70D)));
            auto font = button->font(); font.setFamily(family); font.setPixelSize(16); button->setFont(font);
            QApplication::processEvents(); report(QString(family).replace(' ', '_'));
        }
        presentation.beginTeardown(); return;
    }
    const bool iconFont = button->font().family() == "Segoe Fluent Icons" || button->font().family() == "Segoe MDL2 Assets";
    require(button->text() == (iconFont ? QString(QChar(0xE70D)) : QString::fromUtf8("⌄"))
        && (!iconFont || QRawFont::fromFont(button->font()).supportsCharacter(0xE70D))
        && button->shortcut().isEmpty(), "S8 exact Windows chevron/Unicode fallback/no product shortcut");
    require(button->toolTip() == QCoreApplication::translate(name == QByteArray("launcherHideToTray") ? "LauncherWindow" : "MainWindow",
        "Hide to System Tray"), "S8 frozen translated tooltip");
    require(button->parentWidget() == window.centralWidget() && !button->toolTip().isEmpty(), "S8 client control/tooltip");
    auto* accessible = QAccessible::queryAccessibleInterface(button);
    require(accessible && accessible->role() == QAccessible::Button && accessible->text(QAccessible::Name) == button->toolTip()
        && accessible->actionInterface() && accessible->actionInterface()->actionNames().contains(QAccessibleActionInterface::pressAction()),
        "S8 accessible button name/state/press action");
    const auto checkBounds = [&] {
        const auto point = button->mapTo(window.centralWidget(), QPoint{});
        require(window.centralWidget()->rect().contains(QRect(point, button->size())), "S8 DPI-safe client bounds");
        require(button->width() >= 28 && button->height() >= 24 && button->width() <= 40 && button->height() <= 32,
            "S8 restrained logical hit area");
        require(point.y() >= 2 && point.y() <= 15
            && window.centralWidget()->width() - point.x() - button->width() >= 4
            && window.centralWidget()->width() - point.x() - button->width() <= 20, "S8 top/right client anchors");
        const auto ink = QFontMetrics(button->font()).tightBoundingRect(button->text());
        require(ink.width() >= 9 && ink.height() >= 5 && ink.width() <= button->width() - 8
            && ink.height() <= button->height() - 8, "S8 chevron is legible without dominating the hit area");
    };
    checkBounds();
    QEnterEvent enter(button->rect().center(), button->mapTo(&window, button->rect().center()),
        button->mapToGlobal(button->rect().center()));
    QApplication::sendEvent(button, &enter); QApplication::processEvents(); report("hover");
    button->setDown(true); require(button->isDown(), "S8 pressed control state"); report("pressed");
    button->setDown(false);
    QEvent leave(QEvent::Leave); QApplication::sendEvent(button, &leave);
    const auto hwnd = reinterpret_cast<HWND>(window.winId());
    const auto style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    require((style & (WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX))
        == (WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX), "S8 native caption/resize controls retained");
    window.resize(900, 600); QApplication::processEvents(); checkBounds();
    window.showMaximized(); QApplication::processEvents(); checkBounds();
    window.showNormal(); QApplication::processEvents(); checkBounds();
    qualified = false; presentation.refreshEligibility();
    require(!button->isEnabled() && accessible->state().disabled, "S8 disabled eligibility is accessible"); report("disabled");
    qualified = true; presentation.refreshEligibility();
    QDialog dialog(&window); dialog.setWindowModality(Qt::ApplicationModal); dialog.show(); QApplication::processEvents();
    presentation.refreshEligibility(); require(!button->isEnabled(), "S8 real modal disables client Hide");
    dialog.close(); QApplication::processEvents(); presentation.refreshEligibility();
    require(button->isEnabled(), "S8 modal dismissal restores client eligibility");
    presentation.beginTeardown();
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    try {
        const bool survey = app.arguments().contains("--survey");
        const auto capture = app.arguments().indexOf("--capture-dir");
        const auto directory = capture >= 0 ? app.arguments().value(capture + 1) : QString{};
        QTemporaryDir temporary; require(temporary.isValid(), "S8 temporary settings");
        QSettings settings(temporary.filePath("ui.ini"), QSettings::IniFormat);
        ApplicationPreferencesStore store(settings); ApplicationTranslationState translation;
        LauncherWindow launcher(translation, store);
        inspect(launcher, launcher.trayPresentation(), "launcherHideToTray", directory, survey);
        launcher.close(); QApplication::processEvents();
        // This is a presentation unit fixture, not a fabricated physical Gate display.
        MainWindow session(DesktopSceneEngine::DisplayId::fromTargetDevicePath("S8-control-unit"), translation, store, {});
        inspect(session, session.trayPresentation(), "sessionHideToTray", directory, survey);
        session.close();
        std::cout << "S8 client control " << (survey ? "survey" : "PASS") << std::endl; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << std::endl; return 1; }
}
