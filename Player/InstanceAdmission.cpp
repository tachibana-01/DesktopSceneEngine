#include "InstanceAdmission.h"

#include <QCryptographicHash>

#include <windows.h>
#include <sddl.h>

#include <vector>
#include <exception>

namespace DesktopSceneEngine::Player
{
std::optional<InteractiveInstanceScope> currentInteractiveInstanceScope()
{
    DWORD sessionId = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &sessionId)) return std::nullopt;
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return std::nullopt;
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<unsigned char> buffer(size);
    const bool gotUser = size != 0 && GetTokenInformation(token, TokenUser, buffer.data(), size, &size);
    CloseHandle(token);
    if (!gotUser) return std::nullopt;
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &sid))
        return std::nullopt;
    InteractiveInstanceScope result{QString::fromWCharArray(sid), sessionId};
    LocalFree(sid);
    return result;
}

std::optional<InstanceSlot> InstanceSlot::fromProcessMode(
    const ApplicationProcessMode& mode, const InteractiveInstanceScope& scope)
{
    if (scope.userSid.isEmpty() || std::holds_alternative<ProcessModeParseError>(mode)) return std::nullopt;
    InstanceSlot slot;
    if (const auto* session = std::get_if<DisplaySessionProcessMode>(&mode))
    {
        if (!session->displayId.isPersistentOwnerCapable()) return std::nullopt;
        const auto canonical = session->displayId.canonicalRepresentation();
        if (!canonical) return std::nullopt;
        slot.launcher_ = false;
        slot.canonicalIdentity_ = QByteArray(canonical->data(), static_cast<qsizetype>(canonical->size()));
    }
    const auto userDigest = QCryptographicHash::hash(scope.userSid.toUtf8(), QCryptographicHash::Sha256).toHex();
    const auto identityDigest = QCryptographicHash::hash(slot.canonicalIdentity_, QCryptographicHash::Sha256).toHex();
    const QString name = QStringLiteral("DesktopSceneEngine.Instance.v1.u%1.s%2.%3")
        .arg(QString::fromLatin1(userDigest)).arg(scope.windowsSessionId)
        .arg(slot.launcher_ ? QStringLiteral("Launcher") : QStringLiteral("Session.") + QString::fromLatin1(identityDigest));
    slot.mutexName_ = QStringLiteral("Local\\") + name;
    // Named pipes do not inherit Local\ kernel-object scoping: user AND session are embedded above.
    slot.endpointName_ = name + QStringLiteral(".Activate");
    return slot;
}

InstanceAdmissionOutcome InstanceAdmissionLease::acquire(const InstanceSlot& slot)
{
    if (attempted_)
    {
        nativeError_ = ERROR_INVALID_FUNCTION;
        return InstanceAdmissionOutcome::Error;
    }
    attempted_ = true;
    const auto scope = currentInteractiveInstanceScope();
    if (!scope)
    {
        nativeError_ = ERROR_NO_SUCH_LOGON_SESSION;
        return InstanceAdmissionOutcome::Error;
    }
    const auto sddl = QStringLiteral("D:P(A;;0x00100001;;;%1)").arg(scope->userSid).toStdWString();
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr))
    {
        nativeError_ = GetLastError();
        return InstanceAdmissionOutcome::Error;
    }
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    const auto name = slot.mutexName().toStdWString();
    handle_ = CreateMutexExW(&attributes, name.c_str(), 0, SYNCHRONIZE | MUTEX_MODIFY_STATE);
    nativeError_ = handle_ ? ERROR_SUCCESS : GetLastError();
    LocalFree(descriptor);
    if (!handle_) return InstanceAdmissionOutcome::Error;
    // Object existence / ERROR_ALREADY_EXISTS does NOT establish occupancy. Only this wait does.
    const DWORD result = WaitForSingleObject(handle_, 0);
    if (result == WAIT_TIMEOUT) return InstanceAdmissionOutcome::Occupied;
    if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED)
    {
        nativeError_ = GetLastError();
        return InstanceAdmissionOutcome::Error;
    }
    owned_ = true;
    ownerThread_ = GetCurrentThreadId();
    return result == WAIT_ABANDONED ? InstanceAdmissionOutcome::AbandonedAndAcquired : InstanceAdmissionOutcome::Acquired;
}

InstanceAdmissionLease::~InstanceAdmissionLease()
{
    if (owned_)
    {
        // Fail fast on a programming error instead of pretending a cross-thread release succeeded.
        if (GetCurrentThreadId() != ownerThread_ || !ReleaseMutex(handle_)) std::terminate();
    }
    if (handle_) CloseHandle(handle_);
}
}
