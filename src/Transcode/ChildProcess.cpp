#include "ChildProcess.h"

#include <algorithm>
#include <thread>
#include <vector>

#include <Infrastructure/Error.h>

namespace vsr::transcode {

namespace {

constexpr DWORD kPollMs = 250;

class InheritableHandle {
public:
    explicit InheritableHandle(HANDLE h = nullptr) : handle_(h) {}
    ~InheritableHandle() { Close(); }
    InheritableHandle(const InheritableHandle&) = delete;
    InheritableHandle& operator=(const InheritableHandle&) = delete;

    HANDLE Get() const noexcept { return handle_; }
    HANDLE* Out() noexcept { return &handle_; }
    bool Valid() const noexcept { return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE; }
    void Close() noexcept {
        if (Valid()) CloseHandle(handle_);
        handle_ = nullptr;
    }
    HANDLE Release() noexcept {
        HANDLE h = handle_;
        handle_ = nullptr;
        return h;
    }

private:
    HANDLE handle_ = nullptr;
};

void AppendTail(std::string& tail, const char* data, std::size_t size,
                std::size_t limit) {
    tail.append(data, size);
    if (tail.size() > limit) tail.erase(0, tail.size() - limit);
}

// Reads a pipe until EOF or the process-side write end is closed.
void ReadToEnd(HANDLE pipe, std::string& out, std::size_t limit) {
    char buffer[4096];
    for (;;) {
        DWORD read = 0;
        const BOOL ok = ReadFile(pipe, buffer, static_cast<DWORD>(sizeof(buffer)), &read, nullptr);
        if (!ok || read == 0) return;
        out.append(buffer, read);
        if (out.size() > limit) out.erase(0, out.size() - limit);
    }
}

void ReadLines(HANDLE pipe, const StreamOptions& options, std::size_t& consumed,
               std::string& pending) {
    char buffer[4096];
    for (;;) {
        DWORD read = 0;
        const BOOL ok = ReadFile(pipe, buffer, static_cast<DWORD>(sizeof(buffer)), &read, nullptr);
        if (!ok || read == 0) break;
        if (consumed >= options.stdoutLimitBytes) continue;
        const std::size_t take = std::min<std::size_t>(read, options.stdoutLimitBytes - consumed);
        consumed += take;
        for (std::size_t i = 0; i < take; ++i) {
            const char c = buffer[i];
            if (c == '\n') {
                if (!pending.empty() && pending.back() == '\r') pending.pop_back();
                if (options.onStdoutLine && !pending.empty()) {
                    options.onStdoutLine(pending);
                }
                pending.clear();
            } else {
                pending.push_back(c);
            }
        }
    }
    // Deliver a final unterminated line.
    if (!pending.empty() && options.onStdoutLine) {
        if (pending.back() == '\r') pending.pop_back();
        options.onStdoutLine(pending);
        pending.clear();
    }
}

struct SpawnedChild {
    PROCESS_INFORMATION process{};
    HANDLE stdinWrite = nullptr;
    HANDLE stdoutRead = nullptr;
    HANDLE stderrRead = nullptr;
};

Result<SpawnedChild> Spawn(const std::wstring& executable, const std::wstring& arguments) {
    SECURITY_ATTRIBUTES inherit = {};
    inherit.nLength = sizeof(inherit);
    inherit.bInheritHandle = TRUE;

    InheritableHandle stdoutWrite;
    InheritableHandle stderrWrite;
    InheritableHandle stdinRead;
    HANDLE stdinWrite = nullptr;
    HANDLE stdoutRead = nullptr;
    HANDLE stderrRead = nullptr;
    // CreatePipe's first argument is the read end, the second the write end.
    // stdout/stderr: the child writes, we read. stdin: the child reads, we
    // hold the write end (closed right after spawn, so the child sees EOF).
    if (!CreatePipe(&stdoutRead, stdoutWrite.Out(), &inherit, 0) ||
        !CreatePipe(&stderrRead, stderrWrite.Out(), &inherit, 0) ||
        !CreatePipe(stdinRead.Out(), &stdinWrite, &inherit, 0)) {
        return ErrorFromWin32(GetLastError(), ErrorCode::EncoderUnavailable,
                              L"Could not create the encoder pipe.",
                              L"CreatePipe failed for an ffmpeg child");
    }
    SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stderrRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stdinWrite, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = stdoutWrite.Get();
    startup.hStdError = stderrWrite.Get();
    // A valid stdin handle matters even for tools that never read it: cmd.exe
    // and some FFmpeg builds fail at startup when the inherited handle is NULL.
    startup.hStdInput = stdinRead.Get();

    // The child's CRT takes argv[0] from the command line, so the executable
    // must be its first token; otherwise the first switch is consumed as the
    // program name (ffprobe then treats `error` as an input file).
    const std::wstring commandLineText =
        L"\"" + executable + L"\" " + arguments;
    std::vector<wchar_t> commandLine(commandLineText.begin(), commandLineText.end());
    commandLine.push_back(L'\0');

    PROCESS_INFORMATION process = {};
    const DWORD flags = CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS;
    if (!CreateProcessW(executable.c_str(), commandLine.data(), nullptr, nullptr, TRUE,
                        flags, nullptr, nullptr, &startup, &process)) {
        const DWORD lastError = GetLastError();
        if (stdoutRead != nullptr) CloseHandle(stdoutRead);
        if (stderrRead != nullptr) CloseHandle(stderrRead);
        if (stdinWrite != nullptr) CloseHandle(stdinWrite);
        return ErrorFromWin32(lastError, ErrorCode::EncoderUnavailable,
                              L"Could not start ffmpeg.",
                              L"CreateProcessW failed for " + executable);
    }

    // The child owns the write ends now; close ours so the readers see EOF.
    stdoutWrite.Close();
    stderrWrite.Close();
    stdinRead.Close();

    SpawnedChild child;
    child.process = process;
    child.stdinWrite = stdinWrite;
    child.stdoutRead = stdoutRead;
    child.stderrRead = stderrRead;
    return child;
}

void CloseIfSet(HANDLE& handle) noexcept {
    if (handle != nullptr) {
        CloseHandle(handle);
        handle = nullptr;
    }
}

} // namespace

Result<ChildOutput> RunChildCapture(const std::wstring& executable,
                                    const std::wstring& arguments, DWORD timeoutMs) {
    SpawnedChild child = {};
    auto spawned = Spawn(executable, arguments);
    if (!spawned) return spawned.TakeError();
    child = spawned.Value();
    CloseIfSet(child.stdinWrite);

    ChildOutput output;
    std::thread outReader;
    std::thread errReader;
    if (child.stdoutRead != nullptr) {
        outReader = std::thread([&output, pipe = child.stdoutRead] {
            ReadToEnd(pipe, output.stdoutText, 1024 * 1024);
        });
    }
    if (child.stderrRead != nullptr) {
        errReader = std::thread([&output, pipe = child.stderrRead] {
            ReadToEnd(pipe, output.stderrText, 256 * 1024);
        });
    }

    const DWORD waited = WaitForSingleObject(child.process.hProcess, timeoutMs);
    output.timedOut = waited == WAIT_TIMEOUT;
    if (output.timedOut) TerminateProcess(child.process.hProcess, 1);
    WaitForSingleObject(child.process.hProcess, 5000);
    GetExitCodeProcess(child.process.hProcess, &output.exitCode);

    if (outReader.joinable()) outReader.join();
    if (errReader.joinable()) errReader.join();
    CloseIfSet(child.stdinWrite);
    CloseIfSet(child.stdoutRead);
    CloseIfSet(child.stderrRead);
    CloseHandle(child.process.hThread);
    CloseHandle(child.process.hProcess);
    return output;
}

Result<DWORD> RunChildStream(const std::wstring& executable, const std::wstring& arguments,
                             const StreamOptions& options, std::string& stderrTail,
                             bool& timedOut) {
    SpawnedChild child = {};
    auto spawned = Spawn(executable, arguments);
    if (!spawned) return spawned.TakeError();
    child = spawned.Value();
    CloseIfSet(child.stdinWrite);

    timedOut = false;
    std::size_t consumed = 0;
    std::string pending;
    std::thread outReader;
    std::thread errReader;
    if (child.stdoutRead != nullptr) {
        outReader = std::thread([&consumed, &pending, &options, pipe = child.stdoutRead] {
            ReadLines(pipe, options, consumed, pending);
        });
    }
    if (child.stderrRead != nullptr) {
        errReader = std::thread([&stderrTail, &options, pipe = child.stderrRead] {
            ReadToEnd(pipe, stderrTail, options.stderrTailBytes);
        });
    }

    const ULONGLONG start = GetTickCount64();
    DWORD exitCode = 0;
    for (;;) {
        const DWORD waited = WaitForSingleObject(child.process.hProcess, kPollMs);
        if (waited == WAIT_OBJECT_0) break;
        const bool cancelled = options.cancel != nullptr && options.cancel->load();
        const bool expired =
            options.timeoutMs != 0 && GetTickCount64() - start >= options.timeoutMs;
        if (cancelled || expired) {
            timedOut = expired;
            TerminateProcess(child.process.hProcess, 1);
            WaitForSingleObject(child.process.hProcess, 5000);
            break;
        }
    }
    GetExitCodeProcess(child.process.hProcess, &exitCode);

    if (outReader.joinable()) outReader.join();
    if (errReader.joinable()) errReader.join();
    CloseIfSet(child.stdinWrite);
    CloseIfSet(child.stdoutRead);
    CloseIfSet(child.stderrRead);
    CloseHandle(child.process.hThread);
    CloseHandle(child.process.hProcess);
    return exitCode;
}

} // namespace vsr::transcode
