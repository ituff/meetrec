#include "CrashReporter.h"
#include "Paths.h"

#include <windows.h>
#include <dbghelp.h>

#include <atomic>
#include <cstdio>

#pragma comment(lib, "dbghelp.lib")

namespace vsr {

namespace {

std::atomic<bool> g_dumpsEnabled{false};
std::atomic<bool> g_installed{false};

LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS* info) {
    // Crash-time code must stay allocation-light and exception-free.
    if (g_dumpsEnabled.load(std::memory_order_relaxed) && info != nullptr) {
        const std::filesystem::path dir = CrashReporter::DumpDirectory();
        HANDLE dirHandle = CreateFileW(dir.c_str(), FILE_LIST_DIRECTORY,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                       OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (dirHandle != INVALID_HANDLE_VALUE) CloseHandle(dirHandle);

        wchar_t name[MAX_PATH] = {};
        swprintf_s(name, L"vsr-crash-%lu.dmp", static_cast<unsigned long>(GetCurrentProcessId()));
        const std::filesystem::path file = dir / name;
        HANDLE dump = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (dump != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION mei = {};
            mei.ThreadId = GetCurrentThreadId();
            mei.ExceptionPointers = info;
            mei.ClientPointers = FALSE;
            MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump,
                              MiniDumpNormal, &mei, nullptr, nullptr);
            CloseHandle(dump);
        }
    }
    // Let the process terminate; WER still reports through its own channel.
    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

void CrashReporter::Install(bool enabled) {
    bool expected = false;
    if (!g_installed.compare_exchange_strong(expected, true)) return;
    g_dumpsEnabled.store(enabled, std::memory_order_relaxed);
    SetUnhandledExceptionFilter(&UnhandledFilter);
}

std::filesystem::path CrashReporter::DumpDirectory() {
    return AppDataDir() / L"CrashDumps";
}

} // namespace vsr
