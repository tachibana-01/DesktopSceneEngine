#include <DesktopSceneEngine/Platform/Display.h>

#include <Windows.h>

#include <algorithm>
#include <cwchar>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace
{

using DesktopSceneEngine::Platform::DisplayDescriptor;
using DesktopSceneEngine::Platform::DisplayEnumerationResult;
using DesktopSceneEngine::Platform::DisplayEnumerationStatus;
using DesktopSceneEngine::Platform::DisplayRefreshRate;
using DesktopSceneEngine::DisplayId;

struct MonitorRecord
{
    std::wstring sourceDeviceName{};
    bool primary = false;
};

bool equalLuid(const LUID& left, const LUID& right) noexcept
{
    return left.LowPart == right.LowPart && left.HighPart == right.HighPart;
}

std::string utf8FromWide(const wchar_t* value)
{
    if (value == nullptr || *value == L'\0')
    {
        return {};
    }

    const int length = static_cast<int>(wcslen(value));
    const int required = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value,
        length,
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required <= 0)
    {
        return {};
    }

    std::string utf8(static_cast<std::size_t>(required), '\0');
    const int converted = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value,
        length,
        utf8.data(),
        required,
        nullptr,
        nullptr);
    return converted == required ? utf8 : std::string{};
}

BOOL CALLBACK collectMonitorRecord(
    HMONITOR monitor,
    HDC,
    LPRECT,
    LPARAM userData
)
{
    auto* records = reinterpret_cast<std::vector<MonitorRecord>*>(userData);
    if (records == nullptr)
    {
        return FALSE;
    }

    MONITORINFOEXW information{};
    information.cbSize = sizeof(information);
    if (!GetMonitorInfoW(monitor, &information))
    {
        return FALSE;
    }

    records->push_back(MonitorRecord{
        information.szDevice,
        (information.dwFlags & MONITORINFOF_PRIMARY) != 0U});
    return TRUE;
}

bool collectMonitorRecords(std::vector<MonitorRecord>& records)
{
    return EnumDisplayMonitors(
               nullptr,
               nullptr,
               collectMonitorRecord,
               reinterpret_cast<LPARAM>(&records))
        != FALSE;
}

bool primaryForSourceDevice(
    const std::vector<MonitorRecord>& records,
    const std::wstring& sourceDeviceName
) noexcept
{
    const auto record = std::find_if(
        records.cbegin(),
        records.cend(),
        [&sourceDeviceName](const MonitorRecord& candidate)
        {
            return candidate.sourceDeviceName == sourceDeviceName;
        });
    return record != records.cend() && record->primary;
}

bool queryActivePaths(
    std::vector<DISPLAYCONFIG_PATH_INFO>& paths,
    std::vector<DISPLAYCONFIG_MODE_INFO>& modes
)
{
    constexpr unsigned int MaximumAttempts = 3U;
    for (unsigned int attempt = 0U; attempt < MaximumAttempts; ++attempt)
    {
        UINT32 pathCount = 0U;
        UINT32 modeCount = 0U;
        if (GetDisplayConfigBufferSizes(
                QDC_ONLY_ACTIVE_PATHS,
                &pathCount,
                &modeCount) != ERROR_SUCCESS)
        {
            return false;
        }

        paths.resize(pathCount);
        modes.resize(modeCount);
        const LONG queryResult = QueryDisplayConfig(
            QDC_ONLY_ACTIVE_PATHS,
            &pathCount,
            paths.data(),
            &modeCount,
            modes.data(),
            nullptr);
        if (queryResult == ERROR_SUCCESS)
        {
            paths.resize(pathCount);
            modes.resize(modeCount);
            return true;
        }

        if (queryResult != ERROR_INSUFFICIENT_BUFFER)
        {
            return false;
        }
    }

    return false;
}

bool sourceDeviceNameForPath(
    const DISPLAYCONFIG_PATH_INFO& path,
    std::wstring& sourceDeviceName
)
{
    DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName{};
    sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
    sourceName.header.size = sizeof(sourceName);
    sourceName.header.adapterId = path.sourceInfo.adapterId;
    sourceName.header.id = path.sourceInfo.id;
    if (DisplayConfigGetDeviceInfo(&sourceName.header) != ERROR_SUCCESS
        || sourceName.viewGdiDeviceName[0] == L'\0')
    {
        return false;
    }

    sourceDeviceName = sourceName.viewGdiDeviceName;
    return true;
}

void targetDeviceDetailsForPath(
    const DISPLAYCONFIG_PATH_INFO& path,
    std::wstring& targetDevicePath,
    std::string& friendlyName
)
{
    DISPLAYCONFIG_TARGET_DEVICE_NAME targetName{};
    targetName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
    targetName.header.size = sizeof(targetName);
    targetName.header.adapterId = path.targetInfo.adapterId;
    targetName.header.id = path.targetInfo.id;
    if (DisplayConfigGetDeviceInfo(&targetName.header) != ERROR_SUCCESS)
    {
        return;
    }

    targetDevicePath = targetName.monitorDevicePath;
    friendlyName = utf8FromWide(targetName.monitorFriendlyDeviceName);
}

const DISPLAYCONFIG_SOURCE_MODE* sourceModeForPath(
    const DISPLAYCONFIG_PATH_INFO& path,
    const std::vector<DISPLAYCONFIG_MODE_INFO>& modes
) noexcept
{
    const auto mode = std::find_if(
        modes.cbegin(),
        modes.cend(),
        [&path](const DISPLAYCONFIG_MODE_INFO& candidate)
        {
            return candidate.infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE
                && equalLuid(candidate.adapterId, path.sourceInfo.adapterId)
                && candidate.id == path.sourceInfo.id;
        });
    return mode == modes.cend() ? nullptr : &mode->sourceMode;
}

std::optional<DisplayRefreshRate> refreshRateForPath(
    const DISPLAYCONFIG_PATH_INFO& path
) noexcept
{
    const DISPLAYCONFIG_RATIONAL refreshRate = path.targetInfo.refreshRate;
    if (refreshRate.Numerator == 0U || refreshRate.Denominator == 0U)
    {
        return std::nullopt;
    }

    return DisplayRefreshRate{refreshRate.Numerator, refreshRate.Denominator};
}

bool hasDuplicateIdentity(const std::vector<DisplayDescriptor>& descriptors) noexcept
{
    for (auto left = descriptors.cbegin(); left != descriptors.cend(); ++left)
    {
        if (std::any_of(
                std::next(left),
                descriptors.cend(),
                [left](const DisplayDescriptor& candidate)
                {
                    return candidate.id == left->id;
                }))
        {
            return true;
        }
    }

    return false;
}

void orderAndNumber(std::vector<DisplayDescriptor>& descriptors)
{
    std::sort(
        descriptors.begin(),
        descriptors.end(),
        [](const DisplayDescriptor& left, const DisplayDescriptor& right)
        {
            if (left.primary != right.primary)
            {
                return left.primary;
            }
            if (left.desktopX != right.desktopX)
            {
                return left.desktopX < right.desktopX;
            }
            if (left.desktopY != right.desktopY)
            {
                return left.desktopY < right.desktopY;
            }
            if (left.sourceDeviceName != right.sourceDeviceName)
            {
                return left.sourceDeviceName < right.sourceDeviceName;
            }

            return left.id < right.id;
        });

    bool primaryAssigned = false;
    for (std::size_t index = 0U; index < descriptors.size(); ++index)
    {
        if (descriptors[index].primary)
        {
            descriptors[index].primary = !primaryAssigned;
            primaryAssigned = true;
        }

        descriptors[index].displayNumber = static_cast<int>(index + 1U);
    }
}

}

namespace DesktopSceneEngine::Platform
{

std::optional<DisplayDescriptor> DisplaySnapshot::resolve(const DisplayId& id) const
{
    if (!id.isValid())
    {
        return std::nullopt;
    }

    const DisplayDescriptor* match = nullptr;
    for (const DisplayDescriptor& descriptor : displays)
    {
        if (descriptor.id != id)
        {
            continue;
        }

        if (match != nullptr)
        {
            return std::nullopt;
        }

        match = &descriptor;
    }

    return match != nullptr ? std::optional<DisplayDescriptor>{*match} : std::nullopt;
}

DisplayEnumerationResult enumerateActiveDisplays()
{
    std::vector<DISPLAYCONFIG_PATH_INFO> paths{};
    std::vector<DISPLAYCONFIG_MODE_INFO> modes{};
    if (!queryActivePaths(paths, modes))
    {
        return DisplayEnumerationResult{DisplayEnumerationStatus::ApiFailure, {}};
    }

    std::vector<MonitorRecord> monitorRecords{};
    if (!collectMonitorRecords(monitorRecords))
    {
        return DisplayEnumerationResult{DisplayEnumerationStatus::ApiFailure, {}};
    }

    std::vector<DisplayDescriptor> descriptors{};
    descriptors.reserve(paths.size());
    for (const DISPLAYCONFIG_PATH_INFO& path : paths)
    {
        std::wstring sourceDeviceName{};
        if (!sourceDeviceNameForPath(path, sourceDeviceName))
        {
            return DisplayEnumerationResult{
                DisplayEnumerationStatus::IncompletePathMetadata,
                {}};
        }

        const DISPLAYCONFIG_SOURCE_MODE* sourceMode = sourceModeForPath(path, modes);
        if (sourceMode == nullptr
            || sourceMode->width == 0U || sourceMode->height == 0U
            || sourceMode->width > static_cast<UINT32>((std::numeric_limits<int>::max)())
            || sourceMode->height > static_cast<UINT32>((std::numeric_limits<int>::max)()))
        {
            return DisplayEnumerationResult{
                DisplayEnumerationStatus::IncompletePathMetadata,
                {}};
        }

        const std::string sourceDeviceNameUtf8 = utf8FromWide(sourceDeviceName.c_str());
        if (sourceDeviceNameUtf8.empty())
        {
            return DisplayEnumerationResult{
                DisplayEnumerationStatus::IncompletePathMetadata,
                {}};
        }

        std::wstring targetDevicePath{};
        std::string friendlyName{};
        targetDeviceDetailsForPath(path, targetDevicePath, friendlyName);
        const std::string targetDevicePathUtf8 = utf8FromWide(targetDevicePath.c_str());
        DisplayId id = targetDevicePathUtf8.empty()
            ? DisplayId::fromSourceDeviceName(sourceDeviceNameUtf8)
            : DisplayId::fromTargetDevicePath(targetDevicePathUtf8);

        const bool primary = primaryForSourceDevice(monitorRecords, sourceDeviceName);

        descriptors.push_back(DisplayDescriptor{
            std::move(id),
            std::move(friendlyName),
            sourceDeviceNameUtf8,
            static_cast<int>(sourceMode->position.x),
            static_cast<int>(sourceMode->position.y),
            static_cast<int>(sourceMode->width),
            static_cast<int>(sourceMode->height),
            primary,
            0,
            refreshRateForPath(path)});
    }

    if (hasDuplicateIdentity(descriptors))
    {
        return DisplayEnumerationResult{DisplayEnumerationStatus::DuplicateIdentity, {}};
    }

    orderAndNumber(descriptors);
    return DisplayEnumerationResult{
        DisplayEnumerationStatus::Success,
        DisplaySnapshot{std::move(descriptors)}};
}

}
