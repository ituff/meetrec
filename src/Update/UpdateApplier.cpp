#include "UpdateApplier.h"

#include <fstream>
#include <string>
#include <vector>

#include <Infrastructure/Paths.h>
#include <Infrastructure/Strings.h>

namespace vsr::update {

namespace {

std::wstring PsQuote(const std::filesystem::path& path) {
    std::wstring text = path.wstring();
    std::wstring escaped;
    escaped.reserve(text.size() + 8);
    for (const wchar_t c : text) {
        if (c == L'\'') escaped += L"''";
        else escaped.push_back(c);
    }
    return L"'" + escaped + L"'";
}

} // namespace

std::filesystem::path UpdatesDirectory() {
    return AppDataDir() / L"Updates";
}

bool IsDirectoryWritable(const std::filesystem::path& directory) noexcept {
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec) || ec) return false;
    const std::filesystem::path probe =
        directory / (L".meetrec-write-test-" + std::to_wstring(GetCurrentProcessId()));
    {
        std::ofstream file(probe, std::ios::binary | std::ios::trunc);
        if (!file.good()) return false;
        file.put('x');
    }
    std::filesystem::remove(probe, ec);
    return true;
}

Result<std::filesystem::path> WriteApplyScript(const std::filesystem::path& zipPath,
                                               const std::filesystem::path& installDir,
                                               const std::filesystem::path& exePath,
                                               DWORD waitPid) {
    std::error_code ec;
    std::filesystem::create_directories(UpdatesDirectory(), ec);
    if (ec) {
        return Error(ErrorCode::OutputNotWritable,
                     L"Could not prepare the update.",
                     L"create_directories failed for " + UpdatesDirectory().wstring());
    }
    const std::filesystem::path script = UpdatesDirectory() / L"apply-update.ps1";
    const std::filesystem::path log = UpdatesDirectory() / L"update.log";

    std::wstring text;
    text += L"$ErrorActionPreference = 'Stop'\r\n";
    text += L"$logPath = " + PsQuote(log) + L"\r\n";
    text += L"function Write-UpdateLog([string]$message) {\r\n";
    text += L"    try { Add-Content -LiteralPath $logPath -Value (('{0:yyyy-MM-dd HH:mm:ss} '"
            L" -f (Get-Date)) + $message) -Encoding UTF8 } catch { }\r\n";
    text += L"}\r\n";
    text += L"$install = " + PsQuote(installDir) + L"\r\n";
    text += L"$exe = " + PsQuote(exePath) + L"\r\n";
    text += L"$zip = " + PsQuote(zipPath) + L"\r\n";
    text += L"$staging = $null\r\n";
    text += L"try {\r\n";
    text += L"    Write-UpdateLog 'waiting for MeetRec to exit'\r\n";
    text += L"    try { Wait-Process -Id " + std::to_wstring(waitPid) +
            L" -Timeout 90 -ErrorAction Stop } catch { }\r\n";
    text += L"    Write-UpdateLog 'extracting update package'\r\n";
    text += L"    $staging = Join-Path $env:TEMP ('MeetRecUpdate-' + "
            L"[guid]::NewGuid().ToString('N'))\r\n";
    text += L"    New-Item -ItemType Directory -Force -Path $staging | Out-Null\r\n";
    text += L"    & (Join-Path $env:SystemRoot 'System32\\tar.exe') -xf $zip -C $staging\r\n";
    text += L"    if ($LASTEXITCODE -ne 0) { throw ('tar.exe failed with exit code ' + "
            L"$LASTEXITCODE) }\r\n";
    text += L"    $found = Get-ChildItem -LiteralPath $staging -Recurse -Filter "
            L"'MeetRec.exe' | Select-Object -First 1\r\n";
    text += L"    if ($null -eq $found) { throw 'MeetRec.exe is missing from the update "
            L"package' }\r\n";
    text += L"    $source = Split-Path -Parent $found.FullName\r\n";
    text += L"    Write-UpdateLog ('copying from ' + $source)\r\n";
    text += L"    Copy-Item -Path (Join-Path $source '*') -Destination $install "
            L"-Recurse -Force\r\n";
    text += L"    Write-UpdateLog 'starting the updated MeetRec'\r\n";
    text += L"    Start-Process -FilePath $exe -WorkingDirectory $install\r\n";
    text += L"    Write-UpdateLog 'update finished'\r\n";
    text += L"} catch {\r\n";
    text += L"    Write-UpdateLog ('update failed: ' + $_.Exception.Message)\r\n";
    text += L"    try { Start-Process -FilePath $exe -WorkingDirectory $install } catch { }\r\n";
    text += L"} finally {\r\n";
    text += L"    if ($staging) { Remove-Item -LiteralPath $staging -Recurse -Force "
            L"-ErrorAction SilentlyContinue }\r\n";
    text += L"    Start-Sleep -Seconds 2\r\n";
    text += L"    Remove-Item -LiteralPath $PSCommandPath -Force -ErrorAction SilentlyContinue\r\n";
    text += L"}\r\n";

    std::ofstream file(script, std::ios::binary | std::ios::trunc);
    if (!file.good()) {
        return Error(ErrorCode::OutputNotWritable, L"Could not prepare the update.",
                     L"Failed to write " + script.wstring());
    }
    const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    file.write(reinterpret_cast<const char*>(bom), sizeof(bom));
    const std::string utf8 = WideToUtf8(text);
    file.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    file.close();
    if (!file.good()) {
        return Error(ErrorCode::OutputNotWritable, L"Could not prepare the update.",
                     L"Write failed for " + script.wstring());
    }
    return script;
}

Result<void> LaunchApplyScript(const std::filesystem::path& scriptPath) {
    wchar_t systemRoot[MAX_PATH] = {};
    GetEnvironmentVariableW(L"SystemRoot", systemRoot, MAX_PATH);
    const std::filesystem::path powershell =
        std::filesystem::path(systemRoot) / L"System32" / L"WindowsPowerShell" / L"v1.0" /
        L"powershell.exe";

    std::wstring commandLine = L"\"" + powershell.wstring() +
                               L"\" -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden "
                               L"-File \"" +
                               scriptPath.wstring() + L"\"";
    std::vector<wchar_t> mutableLine(commandLine.begin(), commandLine.end());
    mutableLine.push_back(L'\0');

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(powershell.c_str(), mutableLine.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        return ErrorFromWin32(GetLastError(), ErrorCode::InternalState,
                              L"Could not start the updater.",
                              L"CreateProcessW failed for powershell.exe");
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return Result<void>{};
}

} // namespace vsr::update
