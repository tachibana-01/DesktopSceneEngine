#include "DisplaySessionBootstrap.h"
#include "InstanceAdmission.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QThread>
#include <QUuid>

#include <windows.h>
#include <tlhelp32.h>
#include <UIAutomation.h>
#include <wrl/client.h>

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

int windowsForPid(DWORD pid)
{
    struct Query { DWORD pid; int count = 0; } query{pid};
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

int windows(Child& child) { return windowsForPid(child.pid); }

HWND primaryWindowForPid(DWORD pid)
{
    struct Query { DWORD pid; HWND window = nullptr; } query{pid};
    EnumWindows([](HWND hwnd, LPARAM value) -> BOOL {
        auto& q = *reinterpret_cast<Query*>(value);
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        wchar_t caption[256]{};
        GetWindowTextW(hwnd, caption, 256);
        if (pid == q.pid && wcsstr(caption, L"DesktopSceneEngine") && GetWindow(hwnd, GW_OWNER) == nullptr)
            q.window = hwnd;
        return TRUE;
    }, reinterpret_cast<LPARAM>(&query));
    return query.window;
}

HWND primaryWindow(Child& child) { return primaryWindowForPid(child.pid); }

DWORD latestJoinedPid(Child& session)
{
    session.read();
    const QByteArray marker = "Coordinator Session joined. PID=";
    const auto offset = session.output.lastIndexOf(marker);
    return offset < 0 ? 0 : session.output.mid(offset + marker.size()).split('\n').front().trimmed().toUInt();
}

DWORD admittedLauncherPid();
int playerProcessCount();

// This fixture can create one detached recovery owner via its real Session. Pin
// that lifetime, not the ordinary invocation which may only forward ACTIVATE.
struct RecoveryOwner
{
    HANDLE process = nullptr;
    HANDLE session = nullptr;
    DWORD sessionPid = 0;
    DWORD pid = 0;
    ~RecoveryOwner()
    {
        if (session) (void)discoverAndClose();
        if (process) CloseHandle(process);
        if (session) CloseHandle(session);
    }
    bool discoverAndClose()
    {
        // Failed Join observation is not permission to abandon a real bootstrap.
        // Discovery is cleanup-only; it cannot satisfy the successful Join assertion.
        if (!process && session) (void)waitUntil([&] {
            return retain(admittedLauncherPid(), sessionPid) || playerProcessCount() == 0;
        });
        return process && closeNormally();
    }
    bool bindSession(DWORD value)
    {
        session = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, value);
        if (!session || WaitForSingleObject(session, 0) != WAIT_TIMEOUT) return false;
        sessionPid = value;
        return true;
    }
    bool retain(DWORD candidate, DWORD originatingSession)
    {
        if (process) return candidate == pid && WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
        const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return false;
        PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
        DWORD parentPid = 0;
        if (Process32FirstW(snapshot, &entry)) do {
            if (entry.th32ProcessID == candidate)
                parentPid = entry.th32ParentProcessID;
        } while (Process32NextW(snapshot, &entry));
        CloseHandle(snapshot);
        if (!candidate || (parentPid != originatingSession && parentPid != GetCurrentProcessId())) return false;
        const HANDLE retained = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, candidate);
        if (!retained) return false;
        wchar_t path[32768]{}; DWORD length = 32768;
        FILETIME born{}, exited{}, kernel{}, user{}, parentBorn{}, parentExit{};
        const HANDLE parent = parentPid == originatingSession ? session : GetCurrentProcess();
        const bool lifetimeMatches = parent && GetProcessTimes(retained, &born, &exited, &kernel, &user)
            && GetProcessTimes(parent, &parentBorn, &parentExit, &kernel, &user)
            && CompareFileTime(&born, &parentBorn) >= 0
            && (WaitForSingleObject(parent, 0) == WAIT_TIMEOUT || CompareFileTime(&born, &parentExit) <= 0);
        const bool matches = lifetimeMatches && QueryFullProcessImageNameW(retained, 0, path, &length)
            && QString::fromWCharArray(path, static_cast<int>(length)).replace('\\', '/').compare(
                QString::fromUtf8(P0_PLAYER_EXECUTABLE).replace('\\', '/'), Qt::CaseInsensitive) == 0
            && WaitForSingleObject(retained, 0) == WAIT_TIMEOUT;
        if (!matches) { CloseHandle(retained); return false; }
        process = retained; pid = candidate;
        std::cout << "S7 retained recovery owner pid=" << pid << " Session=" << originatingSession
            << " birth=" << born.dwHighDateTime << ':' << born.dwLowDateTime << std::endl;
        return true;
    }
    bool closeNormally()
    {
        if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT)
            if (const auto hwnd = primaryWindowForPid(pid); hwnd) (void)PostMessageW(hwnd, WM_CLOSE, 0, 0);
        DWORD code = STILL_ACTIVE;
        // Headless, unregistered bootstrap already has an 8s production deadline.
        // Keep cleanup inside the fixture's existing 10s bound; no timeout widening.
        const bool ok = WaitForSingleObject(process, 10000) == WAIT_OBJECT_0
            && GetExitCodeProcess(process, &code) && code == 0;
        std::cout << "S7 recovery owner normal exit=" << ok << " pid=" << pid << " code=" << code << std::endl;
        return ok;
    }
};

int playerProcessCount()
{
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return -1;
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    const auto name = QString::fromUtf8(P0_PLAYER_EXECUTABLE).split('/').back().split('\\').back().toStdWString();
    int count = 0;
    if (!Process32FirstW(snapshot, &entry)) { CloseHandle(snapshot); return -1; }
    do { if (_wcsicmp(entry.szExeFile, name.c_str()) == 0) ++count; } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    return count;
}

DWORD admittedLauncherPid()
{
    const auto scope = currentInteractiveInstanceScope();
    const auto slot = scope ? InstanceSlot::fromProcessMode(LauncherProcessMode{}, *scope) : std::nullopt;
    if (!slot) return 0;
    const auto name = (QStringLiteral("\\\\.\\pipe\\") + slot->endpointName()).toStdWString();
    const HANDLE pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) return 0;
    ULONG owner = 0;
    const bool known = GetNamedPipeServerProcessId(pipe, &owner);
    CloseHandle(pipe);
    return known ? owner : 0;
}

bool admittedLauncherOwner(DWORD pid) { return pid && admittedLauncherPid() == pid; }

bool closeLauncherUi(Child& launcher)
{
    const auto hwnd = primaryWindow(launcher);
    return hwnd && PostMessageW(hwnd, WM_CLOSE, 0, 0)
        && waitUntil([&] { return !IsWindow(hwnd) && windows(launcher) == 0; });
}

QString canonical(const QString& suffix);
std::unique_ptr<Child> launch(const QStringList& args);

bool invokeSessionHide(HWND hwnd)
{
    using Microsoft::WRL::ComPtr;
    ComPtr<IUIAutomation> automation;
    ComPtr<IUIAutomationElement> root, button;
    ComPtr<IUIAutomationCondition> english, chinese, either;
    if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation)))
        || FAILED(automation->ElementFromHandle(hwnd, &root))) return false;
    auto nameCondition = [&](const wchar_t* name, IUIAutomationCondition** result) {
        VARIANT value; VariantInit(&value); value.vt = VT_BSTR; value.bstrVal = SysAllocString(name);
        const auto hr = automation->CreatePropertyCondition(UIA_NamePropertyId, value, result);
        VariantClear(&value); return SUCCEEDED(hr);
    };
    if (!nameCondition(L"Hide to System Tray", &english) || !nameCondition(L"隐藏到系统托盘", &chinese)
        || FAILED(automation->CreateOrCondition(english.Get(), chinese.Get(), &either))
        || FAILED(root->FindFirst(TreeScope_Subtree, either.Get(), &button)) || !button) return false;
    BOOL enabled = FALSE;
    if (FAILED(button->get_CurrentIsEnabled(&enabled)) || !enabled) return false;
    ComPtr<IUIAutomationInvokePattern> invoke;
    return SUCCEEDED(button->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invoke)))
        && SUCCEEDED(invoke->Invoke());
}

bool hiddenCloseExit()
{
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com)) return false;
    struct ComLifetime { ~ComLifetime() { CoUninitialize(); } } lifetime;
    auto coordinator = launch({});
    auto session = launch({"--display-session", canonical("S5-hidden-close-" + QUuid::createUuid().toString())});
    if (!waitUntil([&] { session->read(); return session->engines() == 1 && primaryWindow(*session)
        && session->output.contains("Coordinator Session joined. PID=" + QByteArray::number(coordinator->pid)); })) return false;
    const auto hwnd = primaryWindow(*session);
    if (!waitUntil([&] { return invokeSessionHide(hwnd); }) || !waitUntil([&] { return !IsWindowVisible(hwnd); })) return false;
    if (!closeLauncherUi(*coordinator) || !coordinator->running() || !session->running()) return false;
    if (!PostMessageW(hwnd, WM_CLOSE, 0, 0)) return false; // Real production ordinary Close; tray semantic path is proved separately by real framed peers.
    bool unexpectedlyVisible = false;
    const bool closed = waitUntil([&] {
        if (IsWindow(hwnd) && IsWindowVisible(hwnd)) unexpectedlyVisible = true;
        return !session->running();
    });
    const bool drained = closed && coordinator->process.waitForFinished(5000);
    const bool ok = drained && !unexpectedlyVisible && !IsWindow(hwnd) && session->process.exitStatus() == QProcess::NormalExit
        && session->process.exitCode() == 0 && coordinator->process.exitStatus() == QProcess::NormalExit
        && coordinator->process.exitCode() == 0 && session->engines() == 1;
    std::cout << "S5 production UIA Hide -> last logical Session ordinary Close -> explicit normal Session exit and coordinator drain=" << ok << std::endl;
    return ok;
}

bool sessionVisibility()
{
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com)) return false;
    struct ComLifetime { ~ComLifetime() { CoUninitialize(); } } lifetime;
    auto coordinator = launch({});
    const auto id = canonical("S2-native-" + QUuid::createUuid().toString());
    const QStringList args{"--display-session", id};
    auto session = launch(args);
    if (!waitUntil([&] { session->read(); return session->engines() == 1 && primaryWindow(*session)
        && session->output.contains("Coordinator Session joined. PID=" + QByteArray::number(coordinator->pid)); })) return false;
    const auto hwnd = primaryWindow(*session);
    const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, session->pid);
    if (!process) return false;
    struct ProcessLifetime { HANDLE value; ~ProcessLifetime() { CloseHandle(value); } } native{process};
    if (!waitUntil([&] { return invokeSessionHide(hwnd); })
        || !waitUntil([&] { return !IsWindowVisible(hwnd); })
        || !IsWindow(hwnd) || WaitForSingleObject(process, 0) != WAIT_TIMEOUT || session->engines() != 1) return false;
    auto duplicate = launch(args);
    if (!duplicate->process.waitForFinished(10000) || duplicate->process.exitCode() != 0
        || !waitUntil([&] { return IsWindowVisible(hwnd); }) || primaryWindow(*session) != hwnd || session->engines() != 1) return false;
    if (!waitUntil([&] { return invokeSessionHide(hwnd); }) || !waitUntil([&] { return !IsWindowVisible(hwnd); })) return false;
    // Fault only this test-owned coordinator. S2 must reveal, not supervise/rejoin it.
    coordinator->process.kill(); coordinator->process.waitForFinished(5000);
    if (!waitUntil([&] { return IsWindowVisible(hwnd); }, 5000) || WaitForSingleObject(process, 0) != WAIT_TIMEOUT
        || primaryWindow(*session) != hwnd || session->engines() != 1) return false;
    const bool closed = session->closeNormally();
    std::cout << "S2 production UIA Hide -> same HWND/one Engine -> duplicate ACTIVATE -> Hide -> coordinator death fail visible -> ordinary Close=" << closed << std::endl;
    return closed;
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
    // Keep the coordinator test-owned; do not leave a detached bootstrap between cases.
    std::unique_ptr<Child> coordinator;
    if (session)
    {
        coordinator = launch({});
        if (!waitUntil([&] { return windows(*coordinator) == 1; })) return false;
    }
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
    auto coordinator = launch({});
    if (!waitUntil([&] { return windows(*coordinator) == 1; })) return false;
    const auto nonce = QUuid::createUuid().toString();
    auto a = launch({"--display-session", canonical(nonce + "A")});
    auto b = launch({"--display-session", canonical(nonce + "B")});
    const bool ok = waitUntil([&] { return a->engines() == 1 && b->engines() == 1
        && windows(*a) == 1 && windows(*b) == 1; });
    std::cout << "differentIds live=" << (a->running() + b->running()) << " Engines=" << a->engines() + b->engines() << std::endl;
    return ok;
}

enum class RecoveryOrder { Racing, BootstrapFirst, UnobservedTeardown };

bool restart(bool crash, bool launcherCrash = false, RecoveryOrder order = RecoveryOrder::Racing)
{
    RecoveryOwner recovered; // Declared first: normal direct-child teardown precedes fallback owner cleanup.
    const QStringList args{"--display-session", canonical("restart-" + QUuid::createUuid().toString())};
    auto launcher = launch({});
    auto session = launch(args);
    const DWORD originalLauncherPid = launcher->pid;
    if (!waitUntil([&] { return windows(*launcher) == 1 && session->engines() == 1; })) return false;
    const HWND originalSession = primaryWindow(*session);
    if (order != RecoveryOrder::Racing && !waitUntil([&] { return latestJoinedPid(*session) == originalLauncherPid; })) return false;
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
        if (!recovered.bindSession(session->pid)) return false;
        std::cout << "S7 launcher-crash parent=" << GetCurrentProcessId() << " initialOwner=" << launcher->pid
            << " Session=" << session->pid << " HWND=" << reinterpret_cast<quintptr>(originalSession) << std::endl;
        launcher->process.kill();
        if (!launcher->process.waitForFinished(5000)) return false;
    }
    else
    {
        // Intentional S1 contract change: x retires UI, not the admitted coordinator.
        if (!closeLauncherUi(*launcher) || !launcher->running()) return false;
    }
    if (!session->running()) return false;
    if (launcherCrash)
    {
        launcher.reset();
        if (order == RecoveryOrder::BootstrapFirst && !waitUntil([&] {
            const auto pid = latestJoinedPid(*session);
            return pid && pid != originalLauncherPid && admittedLauncherOwner(pid) && recovered.retain(pid, session->pid);
        })) return false;
        if (order == RecoveryOrder::UnobservedTeardown && !waitUntil([&] {
            const auto pid = admittedLauncherPid();
            return pid && pid != originalLauncherPid;
        })) return false;
        launcher = launch({});
        if (order == RecoveryOrder::UnobservedTeardown)
        {
            // Real B/ACTIVATE/visible Launcher, but deliberately no successful Join
            // observation/retain in this fixture. The guard must discover only its
            // exact Session descendant and close it normally after direct teardown.
            if (!launcher->process.waitForFinished(10000) || launcher->process.exitCode() != 0
                || !waitUntil([&] { return windowsForPid(admittedLauncherPid()) == 1; })) return false;
            std::cout << "S7 unobserved Join: immediate owned teardown, guard-retained=" << bool(recovered.process) << std::endl;
            return !recovered.process && session->closeNormally() && recovered.discoverAndClose();
        }
    }
    else
    {
        auto relaunch = launch({});
        if (!relaunch->process.waitForFinished(10000) || relaunch->process.exitCode() != 0) return false;
    }
    if (launcherCrash)
    {
        // Either the ordinary invocation or the Session bootstrap wins admission.
        // Join names the native verified owner; ACTIVATE must create its sole Launcher.
        if (!waitUntil([&] {
            const auto pid = latestJoinedPid(*session);
            return pid && pid != originalLauncherPid && admittedLauncherOwner(pid)
                && recovered.retain(pid, session->pid) && windowsForPid(pid) == 1;
        })) return false;
        if (recovered.pid != launcher->pid && ((launcher->running() && !launcher->process.waitForFinished(10000))
            || launcher->process.exitStatus() != QProcess::NormalExit || launcher->process.exitCode() != 0
            || launcher->engines() != 0 || windows(*launcher) != 0)) return false;
        if (!waitUntil([&] { return playerProcessCount() == 2; })) return false; // Sole owner plus the retained Session.
        std::cout << "S7 recovery admittedOwner=" << recovered.pid << " ordinarySpawn=" << launcher->pid
            << " LauncherHWND=" << reinterpret_cast<quintptr>(primaryWindowForPid(recovered.pid))
            << " Session output=" << session->output.toStdString() << std::endl;
    }
    else if (!waitUntil([&] { return windows(*launcher) == 1; })) return false;
    auto repeated = launch(args);
    const bool finished = repeated->process.waitForFinished(7000);
    std::cout << "launcherRestart originalSession=" << session->running()
        << " repeatExited=" << finished << " repeatEngines=" << repeated->engines() << std::endl;
    const bool ok = finished && repeated->process.exitCode() == 0 && repeated->engines() == 0 && session->running();
    if (!launcherCrash) return ok;
    if (!ok || session->engines() != 1 || primaryWindow(*session) != originalSession
        || !IsWindow(originalSession) || windowsForPid(recovered.pid) != 1) return false;
    // Closing only direct QProcess children misses a Session-created bootstrap.
    // Close the same Session normally, then the pinned owner; never kill it.
    if (!session->closeNormally() || !recovered.closeNormally()) return false;
    return playerProcessCount() == 0;
}

bool normalClose()
{
    const QStringList args{"--display-session", canonical("close-" + QUuid::createUuid().toString())};
    auto launcher = launch({});
    auto session = launch(args);
    if (!waitUntil([&] { return windows(*launcher) == 1 && session->engines() == 1; })) return false;
    if (!closeLauncherUi(*launcher) || !launcher->running() || !session->running()
        || !session->closeNormally() || !launcher->process.waitForFinished(10000)
        || launcher->process.exitCode() != 0) return false;
    launcher = launch({});
    session = launch(args);
    const bool restarted = waitUntil([&] { return windows(*launcher) == 1 && session->engines() == 1; });
    return restarted && closeLauncherUi(*launcher) && launcher->running() && session->closeNormally()
        && launcher->process.waitForFinished(10000) && launcher->process.exitCode() == 0;
}

bool coordinatorLifetime()
{
    auto launcher = launch({});
    const QStringList args{"--display-session", canonical("S1-lifetime-" + QUuid::createUuid().toString())};
    auto session = launch(args);
    if (!waitUntil([&] { return windows(*launcher) == 1 && windows(*session) == 1 && session->engines() == 1; }))
        throw std::runtime_error("S1 fixture: both real roles and Session Engine must be ready before Launcher Close");
    // A real duplicate admission/ACTIVATE establishes that the Session is servicing its event loop.
    auto duplicateSession = launch(args);
    if (!duplicateSession->process.waitForFinished(10000) || duplicateSession->process.exitCode() != 0
        || duplicateSession->engines() != 0)
        throw std::runtime_error("S1 fixture: Session ACTIVATE did not qualify; not a lifecycle RED");
    const bool uiDestroyed = closeLauncherUi(*launcher);
    // HWND destruction can precede QProcess's exit notification; drain that notification
    // before interpreting a cached QProcess state as coordinator survival.
    launcher->process.waitForFinished(1000);
    const bool coordinatorAlive = launcher->running();
    const bool sessionIntact = session->running() && windows(*session) == 1 && session->engines() == 1;
    std::cout << "S1 LauncherUI-destroyed=" << uiDestroyed << " coordinator-alive=" << coordinatorAlive
        << " Session-intact=" << sessionIntact << std::endl;
    if (!uiDestroyed || !coordinatorAlive || !sessionIntact) return false;
    const HWND originalSession = primaryWindow(*session);
    session->read();
    if (!session->output.contains("Coordinator Session joined. PID=" + QByteArray::number(launcher->pid))) return false;
    auto newSession = launch({"--display-session", canonical("S1-new-admission-" + QUuid::createUuid().toString())});
    if (!waitUntil([&] {
        newSession->read();
        return newSession->engines() == 1 && windows(*newSession) == 1
            && newSession->output.contains("Coordinator Session joined. PID=" + QByteArray::number(launcher->pid));
    }) || windows(*launcher) != 0) return false;
    auto relaunch = launch({});
    if (!relaunch->process.waitForFinished(10000) || relaunch->process.exitCode() != 0
        || !waitUntil([&] { return windows(*launcher) == 1; }) || relaunch->engines() != 0)
        return false;
    if (primaryWindow(*session) != originalSession || launcher->engines() != 0
        || !newSession->closeNormally() || !closeLauncherUi(*launcher) || !launcher->running()
        || !session->closeNormally()) return false;
    std::cout << "S1 new admission, same-coordinator ACTIVATE, original Session HWND/Engine, UI recreation and zero-role drain" << std::endl;
    return launcher->process.waitForFinished(10000) && launcher->process.exitStatus() == QProcess::NormalExit
        && launcher->process.exitCode() == 0;
}

bool coordinatorDirect()
{
    const auto nonce = QUuid::createUuid().toString();
    auto a = launch({"--display-session", canonical("S1-direct-A-" + nonce)});
    auto b = launch({"--display-session", canonical("S1-direct-B-" + nonce)});
    const auto joinedPid = [](Child& child) -> DWORD {
        child.read();
        const QByteArray marker = "Coordinator Session joined. PID=";
        const auto offset = child.output.indexOf(marker);
        if (offset < 0) return 0;
        return child.output.mid(offset + marker.size()).split('\n').front().trimmed().toUInt();
    };
    if (!waitUntil([&] { return joinedPid(*a) && joinedPid(*b) && a->engines() == 1 && b->engines() == 1; })) return false;
    const DWORD pid = joinedPid(*a);
    if (pid != joinedPid(*b) || pid == a->pid || pid == b->pid) return false;
    Child projection; projection.pid = pid;
    if (windows(projection) != 0) return false; // Internal bootstrap must not pop a Launcher.
    const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!process) return false;
    const bool closed = a->closeNormally() && b->closeNormally();
    const bool drained = closed && WaitForSingleObject(process, 10000) == WAIT_OBJECT_0;
    CloseHandle(process);
    std::cout << "S1 direct concurrent Sessions: one headless coordinator, two Engines, normal zero-role exit=" << drained << std::endl;
    return drained;
}

void requireNoExistingPlayer()
{
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot qualify existing Player process inventory");
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    bool exists = false;
    const auto name = QString::fromUtf8(P0_PLAYER_EXECUTABLE).split('/').back().split('\\').back().toStdWString();
    if (!Process32FirstW(snapshot, &entry)) { CloseHandle(snapshot); throw std::runtime_error("Cannot enumerate Player processes"); }
    do { exists = exists || _wcsicmp(entry.szExeFile, name.c_str()) == 0; } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    if (exists) throw std::runtime_error("Pre-existing Player detected; refusing to touch an unrelated role/coordinator");
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const auto name = app.arguments().value(1);
    try
    {
        requireNoExistingPlayer();
        bool ok = false;
        if (name == "launcher") ok = contenders(false, 2, true);
        if (name == "session") ok = contenders(true, 2, true);
        if (name == "launcher-race") ok = contenders(false, 4, false);
        if (name == "session-race") ok = contenders(true, 4, false);
        if (name == "different") ok = different();
        if (name == "restart") ok = restart(false);
        if (name == "crash") ok = restart(true);
        if (name == "launcher-crash") ok = restart(false, true);
        if (name == "launcher-crash-bootstrap") ok = restart(false, true, RecoveryOrder::BootstrapFirst);
        if (name == "launcher-crash-unobserved-teardown") ok = restart(false, true, RecoveryOrder::UnobservedTeardown);
        if (name == "normal-close") ok = normalClose();
        if (name == "coordinator-lifetime") ok = coordinatorLifetime();
        if (name == "coordinator-direct") ok = coordinatorDirect();
        if (name == "session-visibility") ok = sessionVisibility();
        if (name == "hidden-close-exit") ok = hiddenCloseExit();
        if (name.startsWith("launcher-crash") && ok) requireNoExistingPlayer();
        return ok ? 0 : 1;
    }
    catch (const std::exception& e) { std::cerr << e.what() << std::endl; return 1; }
}
