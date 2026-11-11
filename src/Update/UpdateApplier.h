// MeetRec - in-place updater for the portable package.
//
// The running executable cannot be replaced in place, so the app writes a
// one-shot PowerShell script, starts it hidden, and exits. The script waits
// for the process to end, extracts the verified zip, copies the package over
// the install folder, restarts MeetRec, and deletes itself.
#pragma once

#include <windows.h>

#include <filesystem>

#include <Infrastructure/Result.h>

namespace vsr::update {

std::filesystem::path UpdatesDirectory();

bool IsDirectoryWritable(const std::filesystem::path& directory) noexcept;

Result<std::filesystem::path> WriteApplyScript(const std::filesystem::path& zipPath,
                                               const std::filesystem::path& installDir,
                                               const std::filesystem::path& exePath,
                                               DWORD waitPid);

Result<void> LaunchApplyScript(const std::filesystem::path& scriptPath);

} // namespace vsr::update
