#include "ProjectFile.h"

#include "../Json/AuthoredProjectJson.h"
#include "../Json/ProjectJson.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace DesktopSceneEngine
{
namespace Infrastructure
{
namespace Persistence
{

namespace
{

constexpr std::size_t ReadBufferSize = 64U * 1024U;
constexpr std::uint32_t MaximumTemporaryCreateAttempts = 32U;

std::atomic<std::uint64_t> nextTemporaryFileCounter{0U};

std::string pathAsUtf8(const std::filesystem::path& path)
{
    const std::u8string value = path.u8string();
    return {
        reinterpret_cast<const char*>(value.data()),
        value.size()
    };
}

[[noreturn]] void failWin32Operation(
    const char* operation,
    const std::filesystem::path& path,
    DWORD errorCode)
{
    throw std::runtime_error{
        std::string{operation}
        + " failed for '"
        + pathAsUtf8(path)
        + "' (Win32 error "
        + std::to_string(errorCode)
        + ")."};
}

[[noreturn]] void failWin32Finalization(
    const char* operation,
    const std::filesystem::path& target,
    const std::filesystem::path& temporaryPath,
    DWORD errorCode)
{
    throw std::runtime_error{
        std::string{operation}
        + " failed for target '"
        + pathAsUtf8(target)
        + "' using temporary file '"
        + pathAsUtf8(temporaryPath)
        + "' (Win32 error "
        + std::to_string(errorCode)
        + ")."};
}

class Win32Handle
{
public:

    explicit Win32Handle(HANDLE handle) noexcept
        : handle_(handle)
    {
    }

    ~Win32Handle()
    {
        if (handle_ != INVALID_HANDLE_VALUE)
        {
            (void)CloseHandle(handle_);
        }
    }

    Win32Handle(const Win32Handle&) = delete;
    Win32Handle& operator=(const Win32Handle&) = delete;

    [[nodiscard]] HANDLE get() const noexcept
    {
        return handle_;
    }

    [[nodiscard]] bool close(DWORD& errorCode) noexcept
    {
        const HANDLE handle = std::exchange(handle_, INVALID_HANDLE_VALUE);
        if (handle == INVALID_HANDLE_VALUE)
        {
            errorCode = ERROR_SUCCESS;
            return true;
        }

        if (CloseHandle(handle) == FALSE)
        {
            errorCode = GetLastError();
            return false;
        }

        errorCode = ERROR_SUCCESS;
        return true;
    }

private:

    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

class TemporaryFileCleanup
{
public:

    explicit TemporaryFileCleanup(
        const std::filesystem::path& temporaryPath) noexcept
        : temporaryPath_(temporaryPath)
    {
    }

    ~TemporaryFileCleanup()
    {
        if (armed_)
        {
            (void)DeleteFileW(temporaryPath_.c_str());
        }
    }

    TemporaryFileCleanup(const TemporaryFileCleanup&) = delete;
    TemporaryFileCleanup& operator=(const TemporaryFileCleanup&) = delete;

    void release() noexcept
    {
        armed_ = false;
    }

private:

    const std::filesystem::path& temporaryPath_;
    bool armed_ = true;
};

std::string readCompleteFile(
    const std::filesystem::path& path,
    bool rejectEmpty = true)
{
    std::ifstream input{path, std::ios::binary};
    if (!input.is_open())
    {
        throw std::runtime_error{"Unable to open file for reading."};
    }

    std::string text{};
    std::array<char, ReadBufferSize> buffer{};
    while (input)
    {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize bytesRead = input.gcount();
        if (bytesRead > 0)
        {
            text.append(buffer.data(), static_cast<std::size_t>(bytesRead));
        }
    }

    if (input.bad() || !input.eof())
    {
        throw std::runtime_error{"Unable to read complete file."};
    }

    if (rejectEmpty && text.empty())
    {
        throw std::runtime_error{"Authored project file is empty."};
    }

    return text;
}

nlohmann::json parseJsonRejectingDuplicateKeys(const std::string& text)
{
    std::vector<std::unordered_set<std::string>> objectKeys{};
    const nlohmann::json::parser_callback_t callback =
        [&objectKeys](
            int,
            nlohmann::json::parse_event_t event,
            nlohmann::json& parsed)
        {
            switch (event)
            {
            case nlohmann::json::parse_event_t::object_start:
                objectKeys.emplace_back();
                break;
            case nlohmann::json::parse_event_t::key:
            {
                if (objectKeys.empty())
                {
                    throw std::runtime_error{
                        "JSON parser reported a key outside an object."};
                }

                const std::string& key = parsed.get_ref<const std::string&>();
                if (!objectKeys.back().insert(key).second)
                {
                    throw std::runtime_error{
                        "duplicate JSON key '" + key + "'."};
                }
                break;
            }
            case nlohmann::json::parse_event_t::object_end:
                if (objectKeys.empty())
                {
                    throw std::runtime_error{
                        "JSON parser ended an object without a matching start."};
                }
                objectKeys.pop_back();
                break;
            default:
                break;
            }

            return true;
        };

    return nlohmann::json::parse(text, callback);
}

std::filesystem::path temporaryPathFor(
    const std::filesystem::path& target,
    std::uint64_t counter,
    std::uint32_t attempt)
{
    const std::wstring temporaryName = target.filename().wstring()
        + L".tmp."
        + std::to_wstring(GetCurrentProcessId())
        + L"."
        + std::to_wstring(counter)
        + L"."
        + std::to_wstring(attempt);
    return target.parent_path() / temporaryName;
}

void writeTextFileAtomically(
    const std::filesystem::path& target,
    const std::string& text)
{
    if (target.empty())
    {
        throw std::runtime_error{"Authored project target path is empty."};
    }

    const std::uint64_t counter = nextTemporaryFileCounter.fetch_add(
        1U, std::memory_order_relaxed);
    std::filesystem::path temporaryPath{};
    HANDLE rawHandle = INVALID_HANDLE_VALUE;
    DWORD lastCreateError = ERROR_SUCCESS;

    for (std::uint32_t attempt = 0U;
         attempt < MaximumTemporaryCreateAttempts;
         ++attempt)
    {
        temporaryPath = temporaryPathFor(target, counter, attempt);
        rawHandle = CreateFileW(
            temporaryPath.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (rawHandle != INVALID_HANDLE_VALUE)
        {
            break;
        }

        const DWORD createError = GetLastError();
        lastCreateError = createError;
        if (createError != ERROR_FILE_EXISTS
            && createError != ERROR_ALREADY_EXISTS)
        {
            failWin32Operation("CreateFileW", temporaryPath, createError);
        }
    }

    if (rawHandle == INVALID_HANDLE_VALUE)
    {
        throw std::runtime_error{
            "Unable to create a unique authored project temporary file after "
            + std::to_string(MaximumTemporaryCreateAttempts)
            + " attempts for target '"
            + pathAsUtf8(target)
            + "' (Win32 error "
            + std::to_string(lastCreateError)
            + ")."};
    }

    TemporaryFileCleanup temporaryCleanup{temporaryPath};
    Win32Handle temporaryHandle{rawHandle};

    std::size_t offset = 0U;
    constexpr std::size_t MaximumWriteSize =
        static_cast<std::size_t>((std::numeric_limits<DWORD>::max)());
    while (offset < text.size())
    {
        const std::size_t remaining = text.size() - offset;
        const DWORD requested = static_cast<DWORD>(
            remaining < MaximumWriteSize ? remaining : MaximumWriteSize);
        DWORD bytesWritten = 0U;
        if (WriteFile(
                temporaryHandle.get(),
                text.data() + offset,
                requested,
                &bytesWritten,
                nullptr)
            == FALSE)
        {
            failWin32Operation("WriteFile", temporaryPath, GetLastError());
        }

        if (bytesWritten == 0U)
        {
            throw std::runtime_error{
                "WriteFile made zero-byte progress for temporary file '"
                + pathAsUtf8(temporaryPath)
                + "'."};
        }

        offset += static_cast<std::size_t>(bytesWritten);
    }

    if (FlushFileBuffers(temporaryHandle.get()) == FALSE)
    {
        failWin32Operation(
            "FlushFileBuffers", temporaryPath, GetLastError());
    }

    DWORD closeError = ERROR_SUCCESS;
    if (!temporaryHandle.close(closeError))
    {
        failWin32Operation("CloseHandle", temporaryPath, closeError);
    }

    if (ReplaceFileW(
            target.c_str(),
            temporaryPath.c_str(),
            nullptr,
            0,
            nullptr,
            nullptr)
        != FALSE)
    {
        temporaryCleanup.release();
        return;
    }

    const DWORD replaceError = GetLastError();
    if (replaceError != ERROR_FILE_NOT_FOUND)
    {
        failWin32Finalization(
            "ReplaceFileW", target, temporaryPath, replaceError);
    }

    if (MoveFileExW(
            temporaryPath.c_str(),
            target.c_str(),
            MOVEFILE_WRITE_THROUGH)
        == FALSE)
    {
        failWin32Finalization(
            "MoveFileExW", target, temporaryPath, GetLastError());
    }

    temporaryCleanup.release();
}

}

void saveProjectFile(
    const Domain::Project& project,
    const std::filesystem::path& path
)
{
    const std::string filePath = path.string();
    std::ofstream output(path, std::ios::binary | std::ios::trunc);

    if (!output.is_open())
    {
        throw std::runtime_error{
            "Unable to open project file for writing: " + filePath
        };
    }

    const nlohmann::json document = Json::toJson(project);
    output << document.dump(4);

    if (!output)
    {
        throw std::runtime_error{
            "Unable to write project file: " + filePath
        };
    }

    output.close();

    if (output.fail())
    {
        throw std::runtime_error{
            "Unable to close project file after writing: " + filePath
        };
    }
}

Domain::Project loadProjectFile(
    const std::filesystem::path& path
)
{
    const std::string filePath = path.string();
    std::ifstream input(path, std::ios::binary);

    if (!input.is_open())
    {
        throw std::runtime_error{
            "Unable to open project file for reading: " + filePath
        };
    }

    try
    {
        const nlohmann::json document = nlohmann::json::parse(input);
        return Json::fromJson(document);
    }
    catch (const nlohmann::json::parse_error& error)
    {
        throw std::runtime_error{
            "Unable to parse project file '" + filePath + "': " + error.what()
        };
    }
}

void saveAuthoredProjectFile(
    const Domain::AuthoredProject& project,
    const std::filesystem::path& path
)
{
    const std::string filePath = pathAsUtf8(path);
    try
    {
        const nlohmann::json document = Json::toAuthoredProjectJson(project);
        const std::string text = document.dump(4);
        writeTextFileAtomically(path, text);
    }
    catch (const std::exception& error)
    {
        throw std::runtime_error{
            "Unable to save authored project file '"
            + filePath
            + "': "
            + error.what()};
    }
}

Domain::AuthoredProject loadAuthoredProjectFile(
    const std::filesystem::path& path)
{
    const std::string filePath = pathAsUtf8(path);
    try
    {
        const std::string text = readCompleteFile(path);
        const nlohmann::json document = parseJsonRejectingDuplicateKeys(text);
        if (!document.is_object())
        {
            throw std::runtime_error{
                "Authored project JSON root must be an object."};
        }

        const auto typeIterator = document.find("type");
        if (typeIterator == document.end())
        {
            throw std::runtime_error{
                "Authored project JSON is missing the type field."};
        }
        if (!typeIterator->is_string())
        {
            throw std::runtime_error{
                "Authored project JSON type must be a string."};
        }
        if (typeIterator->get_ref<const std::string&>()
            != Json::AuthoredProjectDocumentType)
        {
            throw std::runtime_error{
                "Authored project JSON type must be DesktopSceneEngineProject."};
        }

        const auto versionIterator = document.find("version");
        if (versionIterator == document.end())
        {
            throw std::runtime_error{
                "Authored project JSON is missing the version field."};
        }
        if (!versionIterator->is_string())
        {
            throw std::runtime_error{
                "Authored project JSON version must be a string."};
        }

        const std::string& version =
            versionIterator->get_ref<const std::string&>();
        if (version == "1.0")
        {
            throw std::runtime_error{
                "Authored project JSON version 1.0 is an unsupported pre-release format."};
        }
        if (version == "2.0")
        {
            throw std::runtime_error{
                "Authored project JSON version 2.0 is an unsupported pre-release format."};
        }
        if (version == Json::AuthoredProjectDocumentVersion)
        {
            return Json::fromAuthoredProjectJson(document);
        }

        throw std::runtime_error{
            "Unsupported authored project JSON version '" + version + "'."};
    }
    catch (const std::exception& error)
    {
        throw std::runtime_error{
            "Unable to load authored project file '"
            + filePath
            + "': "
            + error.what()};
    }
}

void validateAuthoredProjectSaveAsDestination(
    const Domain::DisplayOwner& sourceOwner,
    const std::filesystem::path& destination)
{
    const std::string filePath = pathAsUtf8(destination);
    try
    {
        std::error_code statusError{};
        const bool destinationExists = std::filesystem::exists(
            destination, statusError);
        if (statusError)
        {
            throw std::runtime_error{
                "Unable to inspect destination (filesystem error "
                + std::to_string(statusError.value()) + ")."};
        }
        if (!destinationExists)
        {
            return;
        }

        const std::string text = readCompleteFile(destination, false);
        const nlohmann::json envelope = nlohmann::json::parse(
            text, nullptr, false);
        if (envelope.is_discarded() || !envelope.is_object())
        {
            return;
        }

        const auto typeIterator = envelope.find("type");
        if (typeIterator == envelope.end() || !typeIterator->is_string()
            || typeIterator->get_ref<const std::string&>()
                != Json::AuthoredProjectDocumentType)
        {
            return;
        }

        const auto versionIterator = envelope.find("version");
        if (versionIterator == envelope.end())
        {
            throw std::runtime_error{
                "Recognized authored project destination is missing the version field."};
        }
        if (!versionIterator->is_string())
        {
            throw std::runtime_error{
                "Recognized authored project destination version must be a string."};
        }

        const std::string& version =
            versionIterator->get_ref<const std::string&>();
        if (version == "1.0" || version == "2.0")
        {
            return;
        }
        if (version != Json::AuthoredProjectDocumentVersion)
        {
            throw std::runtime_error{
                "Unsupported authored project destination version '"
                + version + "'."};
        }

        const nlohmann::json strictDocument =
            parseJsonRejectingDuplicateKeys(text);
        const Domain::AuthoredProject existingProject =
            Json::fromAuthoredProjectJson(strictDocument);
        if (existingProject.displayOwner != sourceOwner)
        {
            throw std::runtime_error{
                "The destination belongs to a different display session."};
        }
    }
    catch (const std::exception& error)
    {
        throw std::runtime_error{
            "Unable to validate authored project Save As destination '"
            + filePath
            + "': "
            + error.what()};
    }
}

}
}
}
