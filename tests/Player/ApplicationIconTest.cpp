#include "DisplaySessionBootstrap.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QImage>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <QtEndian>

#include <windows.h>
#include <tlhelp32.h>

#include <iostream>
#include <stdexcept>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

QImage canonicalImage(const QSize& size)
{
    // Force decoding, rather than accepting QIcon's lazy isNull() result.
    const auto pixmap = QIcon(QString::fromUtf8(APPLICATION_ICON_SOURCE)).pixmap(size, 1.0);
    require(!pixmap.isNull() && pixmap.size() == size, "Canonical ICO must decode at the requested size");
    return pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
}

quint16 word(const QByteArray& bytes, qsizetype offset)
{
    require(offset >= 0 && offset + 2 <= bytes.size(), "Truncated resource/PE word");
    return qFromLittleEndian<quint16>(bytes.constData() + offset);
}

quint32 dword(const QByteArray& bytes, qsizetype offset)
{
    require(offset >= 0 && offset + 4 <= bytes.size(), "Truncated resource/PE dword");
    return qFromLittleEndian<quint32>(bytes.constData() + offset);
}

QByteArray resource(HMODULE module, LPCWSTR type, WORD id)
{
    const auto info = FindResourceW(module, MAKEINTRESOURCEW(id), type);
    require(info != nullptr, "Expected PE icon resource is absent");
    const auto data = LoadResource(module, info);
    const auto bytes = data ? LockResource(data) : nullptr;
    const auto size = SizeofResource(module, info);
    require(bytes != nullptr && size > 0, "PE icon resource cannot be read");
    return QByteArray(static_cast<const char*>(bytes), size);
}

void verifyPE(const QString& executable)
{
    QFile file(executable);
    require(file.open(QIODevice::ReadOnly), "Cannot read production Player EXE");
    const auto bytes = file.readAll();
    require(file.error() == QFileDevice::NoError && word(bytes, 0) == IMAGE_DOS_SIGNATURE,
        "Player EXE has no valid DOS header");
    const auto pe = static_cast<qsizetype>(dword(bytes, 0x3c));
    require(dword(bytes, pe) == IMAGE_NT_SIGNATURE && word(bytes, pe + 4) == IMAGE_FILE_MACHINE_AMD64,
        "Player must remain a Windows x64 PE");
    require(word(bytes, pe + 24) == IMAGE_NT_OPTIONAL_HDR64_MAGIC
        && word(bytes, pe + 24 + 68) == IMAGE_SUBSYSTEM_WINDOWS_GUI,
        "Player must retain the Windows GUI subsystem");

    struct Module
    {
        HMODULE handle;
        ~Module() { if (handle) FreeLibrary(handle); }
    } module{LoadLibraryExW(reinterpret_cast<LPCWSTR>(executable.utf16()), nullptr,
        LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE)};
    require(module.handle != nullptr, "Cannot map production Player resources");
    const auto group = resource(module.handle, RT_GROUP_ICON, 101);
    require(word(group, 0) == 0 && word(group, 2) == 1 && word(group, 4) == 1 && group.size() == 20,
        "Application icon group must contain the one canonical image");
    require(static_cast<unsigned char>(group[6]) == 0 && static_cast<unsigned char>(group[7]) == 0
        && word(group, 12) == 32, "Application icon source must be 256x256 / 32-bpp");
    const auto imageBytes = resource(module.handle, RT_ICON, word(group, 18));
    require(dword(group, 14) == static_cast<quint32>(imageBytes.size()), "Icon group payload size mismatch");
    const auto decoded = QImage::fromData(imageBytes).convertToFormat(QImage::Format_ARGB32);
    require(!decoded.isNull() && decoded.size() == QSize(256, 256), "PE icon payload must decode to 256x256");
    // Compare direct decodes on both sides. QPixmap premultiplies alpha and
    // rounds translucent RGB, so it is not a reference for the raw PE image.
    const auto source = QImage(QString::fromUtf8(APPLICATION_ICON_SOURCE)).convertToFormat(QImage::Format_ARGB32);
    require(decoded == source, "PE image must match canonical ICO content, not container bytes");
    std::cout << "PE PASS: x64 GUI, RT_GROUP_ICON=101, referenced RT_ICON=" << word(group, 18)
        << ", 256x256/32-bpp decoded canonical content" << std::endl;
}

HWND roleWindow(DWORD expectedPid)
{
    struct Query { DWORD pid; HWND window = nullptr; } query{expectedPid};
    EnumWindows([](HWND window, LPARAM value) -> BOOL {
        auto& query = *reinterpret_cast<Query*>(value);
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        wchar_t title[256]{};
        GetWindowTextW(window, title, 256);
        if (pid == query.pid && IsWindowVisible(window) && GetWindow(window, GW_OWNER) == nullptr
            && wcsstr(title, L"DesktopSceneEngine"))
            query.window = window;
        return TRUE;
    }, reinterpret_cast<LPARAM>(&query));
    return query.window;
}

void requireNoExistingPlayer(const QString& executable)
{
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    require(snapshot != INVALID_HANDLE_VALUE, "Cannot qualify process containment");
    const auto name = QFileInfo(executable).fileName().toStdWString();
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool found = false;
    if (Process32FirstW(snapshot, &entry))
    {
        do { found = found || _wcsicmp(entry.szExeFile, name.c_str()) == 0; }
        while (Process32NextW(snapshot, &entry));
    }
    else
    {
        CloseHandle(snapshot);
        throw std::runtime_error("Cannot enumerate existing Player processes");
    }
    CloseHandle(snapshot);
    require(!found, "Pre-existing Player detected: refusing to forward activation or interfere");
}

struct Child
{
    QProcess process;
    DWORD pid = 0;

    bool closeNormally()
    {
        if (process.state() == QProcess::NotRunning) return false;
        const auto window = roleWindow(pid);
        if (!window || !PostMessageW(window, WM_CLOSE, 0, 0)) return false;
        return process.waitForFinished(5000) && process.exitStatus() == QProcess::NormalExit
            && process.exitCode() == 0;
    }

    ~Child()
    {
        if (process.state() != QProcess::NotRunning && !closeNormally())
        {
            // Failure containment only, never a success substitute or an unrelated process.
            process.kill();
            process.waitForFinished(5000);
        }
    }
};

void verifyWindowIcon(HWND window, WPARAM kind, const char* label)
{
    DWORD_PTR result = 0;
    require(SendMessageTimeoutW(window, WM_GETICON, kind, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK,
        1000, &result) != 0, "WM_GETICON timed out or failed");
    // The returned HICON is borrowed from the production window; never destroy it.
    const auto actual = QImage::fromHICON(reinterpret_cast<HICON>(result)).convertToFormat(QImage::Format_ARGB32);
    require(!actual.isNull(), "Production Qt window must expose an application HICON");
    const auto expectedIcon = canonicalImage(actual.size()).toHICON();
    require(expectedIcon != nullptr, "Cannot render canonical Qt image through the Windows icon path");
    const auto expected = QImage::fromHICON(expectedIcon).convertToFormat(QImage::Format_ARGB32);
    DestroyIcon(expectedIcon);
    // Compare identical-size Qt -> HICON -> QImage pipelines. Exact equality is
    // retained only after the actual pre-change RED and post-change GREEN prove it.
    require(actual == expected, "Production HICON does not match the canonical DSE icon");
    std::cout << label << "=" << actual.width() << "x" << actual.height() << " exact canonical pixels; ";
}

void verifyRole(const QString& executable, const QStringList& arguments, bool session)
{
    requireNoExistingPlayer(executable);
    QTemporaryDir workingDirectory;
    require(workingDirectory.isValid(), "Cannot create isolated working directory");
    Child child;
    auto environment = QProcessEnvironment::systemEnvironment();
    const auto path = environment.value("Path", environment.value("PATH"));
    environment.remove("Path");
    environment.remove("PATH");
    environment.insert("Path", path);
    child.process.setProcessEnvironment(environment);
    child.process.setWorkingDirectory(workingDirectory.path());
    child.process.start(executable, arguments);
    require(child.process.waitForStarted(5000), "Test-owned Player could not start");
    child.pid = static_cast<DWORD>(child.process.processId());
    QElapsedTimer timer;
    timer.start();
    HWND window = nullptr;
    QByteArray output;
    do
    {
        QApplication::processEvents();
        output += child.process.readAllStandardOutput();
        window = roleWindow(child.pid);
        if (window && (!session || output.contains("Desktop Scene Engine initialized."))) break;
        require(child.process.state() != QProcess::NotRunning, "Player exited before owning its ready role");
        QThread::msleep(10);
    } while (timer.elapsed() < 10000);
    require(window && (!session || output.contains("Desktop Scene Engine initialized.")),
        "Production role did not become ready within the bounded startup interval");
    std::cout << (session ? "Session" : "Launcher") << " PID=" << child.pid << ": ";
    verifyWindowIcon(window, ICON_SMALL, "small");
    verifyWindowIcon(window, ICON_BIG, "big");
    require(child.closeNormally(), "Test-owned Player must shut down normally with exit 0");
    require(roleWindow(child.pid) == nullptr, "Test-owned window must disappear on normal close");
    std::cout << "normal close / exit 0 / owned processes=0 PASS" << std::endl;
}

void verifyRuntime(const QString& executable)
{
    verifyRole(executable, {}, false);
    const auto id = DesktopSceneEngine::DisplayId::fromTargetDevicePath(
        ("ApplicationIconTest-" + QUuid::createUuid().toString()).toStdString());
    const auto request = DesktopSceneEngine::Player::makeDisplaySessionLaunchRequest(executable, id);
    require(request.has_value(), "Test Display Session must have a valid persistent-capable identity");
    verifyRole(executable, request->arguments, true);
}
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    try
    {
        require(QApplication::platformName() == QStringLiteral("windows"), "Icon verification requires native Windows Qt");
        const auto mode = application.arguments().value(1, QStringLiteral("all"));
        require(mode == "all" || mode == "pe" || mode == "runtime", "Expected all, pe or runtime mode");
        const auto executable = QString::fromUtf8(APPLICATION_ICON_PLAYER);
        if (mode != "runtime") verifyPE(executable);
        if (mode != "pe") verifyRuntime(executable);
        std::cout << "ApplicationIconTest " << mode.toStdString() << " PASS" << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "ApplicationIconTest FAIL: " << error.what() << std::endl;
        return 1;
    }
}
