#include "DisplaySessionBootstrap.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QThread>
#include <QUuid>

#include <windows.h>

#include <functional>
#include <iostream>
#include <memory>
#include <vector>

namespace
{
using namespace DesktopSceneEngine::Player;

struct Child
{
    QProcess process;
    QByteArray output;
    DWORD pid = 0;

    void read() { output += process.readAllStandardOutput(); }
    bool running() { return process.state() != QProcess::NotRunning; }
    int engines() { read(); return output.count("Desktop Scene Engine initialized."); }
    bool closeNormally()
    {
        if (!running()) return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
        EnumWindows([](HWND hwnd, LPARAM value) -> BOOL {
            DWORD owner = 0;
            GetWindowThreadProcessId(hwnd, &owner);
            if (owner == static_cast<DWORD>(value)) PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return TRUE;
        }, static_cast<LPARAM>(pid));
        return process.waitForFinished(5000) && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    }
    ~Child()
    {
        if (!closeNormally() && running())
        {
            // Only a child created by this isolated test is ever terminated.
            process.kill();
            process.waitForFinished(5000);
        }
    }
};

bool waitUntil(const std::function<bool()>& predicate, int timeout = 10000)
{
    QElapsedTimer time;
    time.start();
    do
    {
        QCoreApplication::processEvents();
        if (predicate()) return true;
        QThread::msleep(10);
    } while (time.elapsed() < timeout);
    return false;
}

int windows(Child& child)
{
    struct Query { DWORD pid; int count = 0; } query{child.pid};
    EnumWindows([](HWND hwnd, LPARAM value) -> BOOL {
        auto& q = *reinterpret_cast<Query*>(value);
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        wchar_t caption[256]{};
        GetWindowTextW(hwnd, caption, 256);
        if (pid == q.pid && wcsstr(caption, L"DesktopSceneEngine") && GetWindow(hwnd, GW_OWNER) == nullptr)
            ++q.count;
        return TRUE;
    }, reinterpret_cast<LPARAM>(&query));
    return query.count;
}

QString canonical(const QString& suffix)
{
    const auto id = DesktopSceneEngine::DisplayId::fromTargetDevicePath(suffix.toStdString());
    const auto request = makeDisplaySessionLaunchRequest(QStringLiteral("unused"), id);
    return request->arguments.back();
}

std::unique_ptr<Child> launch(const QStringList& args)
{
    auto child = std::make_unique<Child>();
    auto env = QProcessEnvironment::systemEnvironment();
    const auto path = env.value("Path", env.value("PATH"));
    env.remove("PATH");
    env.remove("Path");
    env.insert("Path", path);
    child->process.setProcessEnvironment(env);
    child->process.start(QString::fromUtf8(P0_PLAYER_EXECUTABLE), args);
    if (!child->process.waitForStarted(5000)) throw std::runtime_error("Player child failed to start");
    child->pid = static_cast<DWORD>(child->process.processId());
    return child;
}

bool contenders(bool session, int count, bool sequential)
{
    const QString id = canonical("P0-process-" + QUuid::createUuid().toString());
    const QStringList args = session ? QStringList{"--display-session", id} : QStringList{};
    std::vector<std::unique_ptr<Child>> children;
    for (int i = 0; i < count; ++i)
    {
        children.push_back(launch(args));
        if (i == 0 && sequential && !waitUntil([&] { return windows(*children[0]) == 1
            && (!session || children[0]->engines() == 1); })) return false;
    }
    if (!waitUntil([&] {
        for (auto& c : children)
            if (c->running() && (windows(*c) != 1 || (session && c->engines() == 0))) return false;
        return true;
    })) return false;
    // Give ACTIVATE forwarding a bounded opportunity to complete; pre-fix owners remain live.
    waitUntil([&] {
        int live = 0;
        for (auto& c : children) live += c->running() ? 1 : 0;
        return live == 1;
    }, 1500);
    int live = 0, roleWindows = 0, engines = 0, forwarded = 0;
    for (auto& c : children)
    {
        live += c->running() ? 1 : 0;
        roleWindows += windows(*c);
        engines += c->engines();
        forwarded += !c->running() && c->process.exitStatus() == QProcess::NormalExit && c->process.exitCode() == 0 ? 1 : 0;
    }
    std::cout << "contenders=" << count << " live=" << live << " roleWindows=" << roleWindows
        << " Engines=" << engines << " successfulForwarders=" << forwarded << std::endl;
    return live == 1 && roleWindows == 1 && engines == (session ? 1 : 0) && forwarded == count - 1;
}

bool different()
{
    const auto nonce = QUuid::createUuid().toString();
    auto a = launch({"--display-session", canonical(nonce + "A")});
    auto b = launch({"--display-session", canonical(nonce + "B")});
    const bool ok = waitUntil([&] { return a->engines() == 1 && b->engines() == 1
        && windows(*a) == 1 && windows(*b) == 1; });
    std::cout << "differentIds live=" << (a->running() + b->running()) << " Engines=" << a->engines() + b->engines() << std::endl;
    return ok;
}

bool restart(bool crash, bool launcherCrash = false)
{
    const QStringList args{"--display-session", canonical("restart-" + QUuid::createUuid().toString())};
    auto launcher = launch({});
    auto session = launch(args);
    if (!waitUntil([&] { return windows(*launcher) == 1 && session->engines() == 1; })) return false;
    if (crash)
    {
        session->process.kill();
        if (!session->process.waitForFinished(5000)) return false;
        session.reset();
        session = launch(args);
        const bool ok = waitUntil([&] { return session->engines() == 1 && windows(*session) == 1; });
        std::cout << "crashRecovery Engines=" << session->engines() << std::endl;
        return ok;
    }
    if (launcherCrash)
    {
        launcher->process.kill();
        if (!launcher->process.waitForFinished(5000)) return false;
    }
    else if (!launcher->closeNormally()) return false;
    launcher.reset();
    if (!session->running()) return false;
    launcher = launch({});
    if (!waitUntil([&] { return windows(*launcher) == 1; })) return false;
    auto repeated = launch(args);
    const bool finished = repeated->process.waitForFinished(7000);
    std::cout << "launcherRestart originalSession=" << session->running()
        << " repeatExited=" << finished << " repeatEngines=" << repeated->engines() << std::endl;
    return finished && repeated->process.exitCode() == 0 && repeated->engines() == 0 && session->running();
}

bool normalClose()
{
    const QStringList args{"--display-session", canonical("close-" + QUuid::createUuid().toString())};
    auto launcher = launch({});
    auto session = launch(args);
    if (!waitUntil([&] { return windows(*launcher) == 1 && session->engines() == 1; })) return false;
    if (!launcher->closeNormally() || !session->running() || !session->closeNormally()) return false;
    launcher = launch({});
    session = launch(args);
    const bool restarted = waitUntil([&] { return windows(*launcher) == 1 && session->engines() == 1; });
    return restarted && launcher->closeNormally() && session->closeNormally();
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const auto name = app.arguments().value(1);
    try
    {
        bool ok = false;
        if (name == "launcher") ok = contenders(false, 2, true);
        if (name == "session") ok = contenders(true, 2, true);
        if (name == "launcher-race") ok = contenders(false, 4, false);
        if (name == "session-race") ok = contenders(true, 4, false);
        if (name == "different") ok = different();
        if (name == "restart") ok = restart(false);
        if (name == "crash") ok = restart(true);
        if (name == "launcher-crash") ok = restart(false, true);
        if (name == "normal-close") ok = normalClose();
        return ok ? 0 : 1;
    }
    catch (const std::exception& e) { std::cerr << e.what() << std::endl; return 1; }
}
