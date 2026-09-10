#include "OutputFile.h"

#include <shlobj.h>

#include <Infrastructure/Strings.h>

#include <cstdio>
#include <ctime>

namespace vsr::app {

std::wstring OutputStampFromMs(std::int64_t systemMs) {
    const std::time_t seconds = static_cast<std::time_t>(systemMs / 1000);
    std::tm tmLocal{};
    localtime_s(&tmLocal, &seconds);
    wchar_t stamp[32];
    swprintf_s(stamp, L"Recording_%04d%02d%02d_%02d%02d%02d", tmLocal.tm_year + 1900,
               tmLocal.tm_mon + 1, tmLocal.tm_mday, tmLocal.tm_hour, tmLocal.tm_min,
               tmLocal.tm_sec);
    return stamp;
}

namespace {

std::filesystem::path DefaultVideosDir() {
    // Fallback used when the user has never chosen a directory.
    PWSTR raw = nullptr;
    std::filesystem::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Videos, 0, nullptr, &raw))) {
        result = std::filesystem::path(raw);
        CoTaskMemFree(raw);
    } else {
        wchar_t profile[MAX_PATH] = {};
        if (GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH) > 0) {
            result = std::filesystem::path(std::wstring(profile) + L"\\Videos");
        } else {
            result = std::filesystem::path(L"C:\\");
        }
    }
    return result / L"Virtual Screen Recorder";
}

} // namespace


namespace {
bool IsSafeUserPath(const std::wstring& path) {
    if (path.empty()) return true;
    if (path.rfind(L"\\.\\", 0) == 0) return false;
    if (path.find(L"..") != std::wstring::npos) return false;
    return true;
}
} // namespace

Result<OutputFilePlan> PlanOutputFile(const std::filesystem::path& directory, IClock& clock) {
    std::filesystem::path dir = directory;
    if (dir.empty()) {
        dir = DefaultVideosDir();
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (!std::filesystem::is_directory(dir)) {
        return ErrorFromWin32(ERROR_PATH_NOT_FOUND, ErrorCode::OutputNotWritable,
                              L"The output folder could not be used. Pick a folder on a local "
                              L"drive and try again.",
                              L"create_directories/is_directory failed for " + dir.wstring(),
                              true);
    }

    const std::wstring stamp = OutputStampFromMs(clock.SystemTimeMs());
    OutputFilePlan plan;

    // FR-054: never overwrite. FR-057: the partial variant must also be free.
    for (int suffix = 0;; ++suffix) {
        std::wstring name = stamp;
        if (suffix > 0) {
            wchar_t buf[16];
            swprintf_s(buf, L"_%d", suffix);
            name += buf;
        }
        plan.finalPath = dir / (name + L".mp4");
        plan.partialPath = dir / (name + L".partial.mp4");
        if (!std::filesystem::exists(plan.finalPath, ec) &&
            !std::filesystem::exists(plan.partialPath, ec)) {
            break;
        }
        if (suffix >= 999) {
            return Error(ErrorCode::OutputNotWritable,
                         L"Too many recordings with the same name exist in this folder. Choose "
                         L"another folder.",
                         L"Collision suffix exhausted", true);
        }
    }
    return plan;
}

Result<void> ValidateOutputDirectory(const std::filesystem::path& directory,
                                     std::uintmax_t requiredBytes) {
    if (directory.empty()) {
        return Error(ErrorCode::OutputNotWritable,
                     L"Choose an output folder before starting a recording.", L"Empty directory",
                     S_OK, true);
    }
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec)) {
        return ErrorFromWin32(ERROR_PATH_NOT_FOUND, ErrorCode::OutputNotWritable,
                              L"The output folder does not exist. Create it or pick another.",
                              L"is_directory failed for " + directory.wstring(),
                              true);
    }

    // Writability probe: create and delete a uniquely named temp file.
    wchar_t name[64];
    swprintf_s(name, L"vsr-write-probe-%08x.tmp", GetTickCount());
    const std::filesystem::path probe = directory / name;
    HANDLE file =
        CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                    FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD err = GetLastError();
        DeleteFileW(probe.c_str());
        return ErrorFromWin32(err, ErrorCode::OutputNotWritable,
                              L"The output folder is not writable. Choose a folder where your "
                              L"user account has write access.",
                              L"Write probe failed", true);
    }
    CloseHandle(file);

    ULARGE_INTEGER freeBytes{};
    if (!GetDiskFreeSpaceExW(directory.c_str(), &freeBytes, nullptr, nullptr)) {
        return ErrorFromWin32(GetLastError(), ErrorCode::OutputNotWritable,
                              L"Free space could not be checked for the output folder.",
                              L"GetDiskFreeSpaceExW failed", true);
    }
    if (freeBytes.QuadPart < requiredBytes) {
        return ErrorFromWin32(ERROR_DISK_FULL, ErrorCode::OutputDiskLow,
                              L"Not enough free disk space for this recording. Free up space or "
                              L"pick a different drive.",
                              L"Free bytes below required bytes", true);
    }
    return Result<void>{};
}

} // namespace vsr::app
