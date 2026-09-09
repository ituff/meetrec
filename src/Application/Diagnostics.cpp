#include "Diagnostics.h"

#include <DisplayControl/TopologyService.h>
#include <Infrastructure/Json.h>
#include <Infrastructure/Log.h>
#include <Infrastructure/Paths.h>
#include <Infrastructure/Strings.h>

#include <shellapi.h>
#include <shlobj.h>

#include <cstdio>

namespace vsr::app {

namespace {

std::string WriteMetadataFile(const DiagnosticsContext& context) {
    json::Value root{json::Object{}};
    root.Set("type", json::Value("vsr-diagnostics"));
    root.Set("appVersion", json::Value(WideToUtf8(context.appVersion)));
    root.Set("correlationId", json::Value(WideToUtf8(context.correlationId)));
    root.Set("osBuild", json::Value(WideToUtf8(context.osBuild)));
    root.Set("gpuDescription", json::Value(WideToUtf8(context.gpuDescription)));
    root.Set("encoder", json::Value(WideToUtf8(context.encoderName)));
    root.Set("encoderHardware", json::Value(context.encoderHardware));
    root.Set("virtualDisplayDriverPresent",
             json::Value(WideToUtf8(context.driverDevicePresent)));

    // Sanitized display topology: monitor names and geometry only — no
    // window titles, process names, or document content (FR-094).
    json::Value monitors{json::Array{}};
    for (const auto& m : vsr::display::TopologyService::Summarize().monitors) {
        json::Value entry{json::Object{}};
        entry.Set("name", json::Value(WideToUtf8(m.friendlyName)));
        entry.Set("device", json::Value(WideToUtf8(m.gdiDeviceName)));
        char rect[64];
        std::snprintf(rect, sizeof(rect), "%ldx%ld @ %ld,%ld", m.w, m.h, m.x, m.y);
        entry.Set("mode", json::Value(rect));
        entry.Set("dpi", json::Value(static_cast<double>(m.dpiX)));
        entry.Set("isVirtual", json::Value(m.isVirtual));
        entry.Set("primary", json::Value(m.primary));
        monitors.Add(std::move(entry));
    }
    root.Set("monitors", std::move(monitors));

    const std::filesystem::path path = vsr::LogDir() / L"diagnostics.json";
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") == 0 && f != nullptr) {
        const std::string text = root.Serialize();
        fwrite(text.data(), 1, text.size(), f);
        fclose(f);
    }
    return WideToUtf8(path.filename().wstring());
}

} // namespace

Result<std::filesystem::path> CreateDiagnosticsPackage(const DiagnosticsContext& context) {
    namespace fs = std::filesystem;

    const fs::path logDir = vsr::LogDir();
    const std::string metadataName = WriteMetadataFile(context);
    log::Info("Diagnostics", "DIAGNOSTICS_PACKAGE_STARTED", context.correlationId,
              L"Diagnostics package creation started.",
              {{"metadata", metadataName}});

    const std::wstring stampName = L"vsr-diagnostics-" + context.correlationId.substr(0, 8);
    const fs::path zipPath = vsr::DiagnosticsDir() / (stampName + L".zip");
    DeleteFileW(zipPath.c_str());

    // Best-effort archive via PowerShell (in-box since Windows 10). On any
    // failure the log folder itself is handed over instead (FR-096 SHOULD).
    std::wstring command =
        L"/c powershell -NoProfile -ExecutionPolicy Bypass -Command \"Compress-Archive -Path '" +
        logDir.wstring() + L"\\*' -DestinationPath '" + zipPath.wstring() + L"' -Force\"";

    SHELLEXECUTEINFOW exec = {};
    exec.cbSize = sizeof(exec);
    exec.lpFile = L"cmd.exe";
    exec.lpParameters = command.c_str();
    exec.fMask = SEE_MASK_NOASYNC | SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    exec.nShow = SW_HIDE;
    if (!ShellExecuteExW(&exec) || exec.hProcess == nullptr) {
        log::Warning("Diagnostics", "DIAGNOSTICS_ARCHIVE_UNAVAILABLE", context.correlationId,
                     L"Archiver unavailable; returning the log folder instead.");
        return logDir;
    }
    const DWORD wait = WaitForSingleObject(exec.hProcess, 30000);
    DWORD exitCode = 1;
    GetExitCodeProcess(exec.hProcess, &exitCode);
    CloseHandle(exec.hProcess);

    if (wait == WAIT_OBJECT_0 && exitCode == 0 && fs::exists(zipPath)) {
        log::Info("Diagnostics", "DIAGNOSTICS_PACKAGE_CREATED", context.correlationId,
                  L"Diagnostics package created.");
        return zipPath;
    }
    return logDir;
}

} // namespace vsr::app
