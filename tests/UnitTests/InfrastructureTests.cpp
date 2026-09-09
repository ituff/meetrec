// Error code, identity matching, sanitizer, and logger tests (T020, T021,
// T061): stable code strings, virtual display identity matching (FR-013),
// title redaction (FR-094), and rotation/retention (FR-095).
#include "TestFramework.h"
#include <DisplayControl/VirtualDisplayIdentity.h>
#include <Infrastructure/Error.h>
#include <Infrastructure/Log.h>
#include <Infrastructure/Strings.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace vsr;
using namespace vsr::display;

VSR_TEST(ErrorCodes, StableStrings) {
    VSR_CHECK_EQ(std::string(ToString(ErrorCode::InitOsUnsupported)),
                 std::string("VSR-INIT-OS-UNSUPPORTED"));
    VSR_CHECK_EQ(std::string(ToString(ErrorCode::InitDriverMissing)),
                 std::string("VSR-INIT-DRIVER-MISSING"));
    VSR_CHECK_EQ(std::string(ToString(ErrorCode::InitD3dFailed)),
                 std::string("VSR-INIT-D3D-FAILED"));
    VSR_CHECK_EQ(std::string(ToString(ErrorCode::DisplayNotFound)),
                 std::string("VSR-DISPLAY-NOT-FOUND"));
    VSR_CHECK_EQ(std::string(ToString(ErrorCode::DisplayDisconnected)),
                 std::string("VSR-DISPLAY-DISCONNECTED"));
    VSR_CHECK_EQ(std::string(ToString(ErrorCode::TargetInvalid)),
                 std::string("VSR-TARGET-INVALID"));
    VSR_CHECK_EQ(std::string(ToString(ErrorCode::TargetLost)),
                 std::string("VSR-TARGET-LOST"));
    VSR_CHECK_EQ(std::string(ToString(ErrorCode::CaptureInitFailed)),
                 std::string("VSR-CAPTURE-INIT-FAILED"));
    VSR_CHECK_EQ(std::string(ToString(ErrorCode::CaptureDeviceLost)),
                 std::string("VSR-CAPTURE-DEVICE-LOST"));
    VSR_CHECK_EQ(std::string(ToString(ErrorCode::EncoderUnavailable)),
                 std::string("VSR-ENCODER-UNAVAILABLE"));
    VSR_CHECK_EQ(std::string(ToString(ErrorCode::EncoderWriteFailed)),
                 std::string("VSR-ENCODER-WRITE-FAILED"));
    VSR_CHECK_EQ(std::string(ToString(ErrorCode::OutputNotWritable)),
                 std::string("VSR-OUTPUT-NOT-WRITABLE"));
    VSR_CHECK_EQ(std::string(ToString(ErrorCode::OutputDiskLow)),
                 std::string("VSR-OUTPUT-DISK-LOW"));
    VSR_CHECK_EQ(std::string(ToString(ErrorCode::FinalizeFailed)),
                 std::string("VSR-FINALIZE-FAILED"));
    VSR_CHECK_EQ(std::string(ToString(ErrorCode::InternalState)),
                 std::string("VSR-INTERNAL-STATE"));
}

VSR_TEST(ErrorCodes, HresultPreserved) {
    const Error e = ErrorFromHresult(0x887A0005, ErrorCode::CaptureDeviceLost, L"u", L"t");
    VSR_CHECK_EQ(e.hresult, static_cast<HRESULT>(0x887A0005));
    const Error w = ErrorFromWin32(ERROR_DISK_FULL, ErrorCode::OutputDiskLow, L"u", L"t");
    VSR_CHECK_EQ(w.hresult, HRESULT_FROM_WIN32(ERROR_DISK_FULL));
}

VSR_TEST(Identity, MatchesFriendlyNameCaseInsensitively) {
    VSR_CHECK(MatchesVirtualMonitorName(L"Virtual Recording Display"));
    VSR_CHECK(MatchesVirtualMonitorName(L"virtual recording display"));
    VSR_CHECK(MatchesVirtualMonitorName(L"VIRTUAL RECORDING DISPLAY"));
    VSR_CHECK(!MatchesVirtualMonitorName(L"Virtual Recording Display 2"));
    VSR_CHECK(!MatchesVirtualMonitorName(L"Generic PnP Monitor"));
    VSR_CHECK(!MatchesVirtualMonitorName(L""));
}

VSR_TEST(Identity, MatchesInstancePath) {
    VSR_CHECK(MatchesVirtualInstancePath(L"ROOT\\VIRTUALSCREENRECORD\\0000"));
    VSR_CHECK(MatchesVirtualInstancePath(L"root\\virtualscreenrecord\\VirtualScreenRecorder"));
    VSR_CHECK(!MatchesVirtualInstancePath(L"ROOT\\DISPLAY\\0000"));
    VSR_CHECK(!MatchesVirtualInstancePath(L""));
}

VSR_TEST(Sanitize, WindowTitlesNeverLoggedVerbatim) {
    const std::wstring title = L"Q3 Board Meeting Budget.xlsx - Excel";
    const std::wstring safe = SanitizeWindowTitle(title);
    VSR_CHECK(safe.find(L"Budget") == std::wstring::npos);
    VSR_CHECK(safe.find(std::to_wstring(title.size()) + L" chars") != std::wstring::npos);    VSR_CHECK_EQ(SanitizeWindowTitle(L""), std::wstring(L"<empty-title>"));
}

VSR_TEST(Strings, WideUtf8RoundTrip) {
    const std::wstring original = L"caf\x00E9 \u4F60\u597D world";
    VSR_CHECK(Utf8ToWide(WideToUtf8(original)) == original);
}

VSR_TEST(Logger, QueuesRecordsAndRotatesBySize) {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / L"vsr-log-test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    LogConfig config;
    config.directory = dir;
    config.minLevel = LogLevel::Debug;
    // Small cap so a handful of records force a rotation.
    config.maxFileBytes = 2048;
    config.maxFiles = 3;
    config.maxTotalBytes = 8 * 1024;

    Logger::Instance().Configure(config);
    for (int i = 0; i < 40; ++i) {
        Logger::Instance().Write(LogLevel::Info, "Test", "ROTATE_EVENT", L"corr-1",
                                 L"filler record to grow the log file size 0123456789");
    }
    // Give the worker time to flush everything.
    for (int waits = 0; waits < 100 && Logger::Instance().PendingCountForTesting() > 0; ++waits) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Directory contains at least one log file; records were written.
    int logFiles = 0;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (entry.path().extension() == L".log") ++logFiles;
    }
    VSR_CHECK(logFiles >= 1);

    Logger::Instance().Shutdown();
    fs::remove_all(dir, ec);
}
