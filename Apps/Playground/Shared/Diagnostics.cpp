#include "Diagnostics.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#define BN_STRINGIFY_IMPL(value) #value
#define BN_STRINGIFY(value) BN_STRINGIFY_IMPL(value)

#if defined(__clang__)
#define BN_COMPILER_NAME "Clang " __clang_version__
#elif defined(_MSC_VER)
#define BN_COMPILER_NAME "MSVC " BN_STRINGIFY(_MSC_VER)
#elif defined(__GNUC__)
#define BN_COMPILER_NAME "GCC " __VERSION__
#else
#define BN_COMPILER_NAME "Unknown"
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
#define BN_CPU_NAME "arm64"
#elif defined(__arm__) || defined(_M_ARM)
#define BN_CPU_NAME "arm"
#elif defined(__x86_64__) || defined(_M_X64)
#define BN_CPU_NAME "x64"
#elif defined(__i386__) || defined(_M_IX86)
#define BN_CPU_NAME "x86"
#else
#define BN_CPU_NAME "unknown"
#endif

#if defined(__APPLE__)
#define BN_PLATFORM_NAME "Apple"
#elif defined(__ANDROID__)
#define BN_PLATFORM_NAME "Android"
#elif defined(_WIN32)
#define BN_PLATFORM_NAME "Windows"
#elif defined(__linux__)
#define BN_PLATFORM_NAME "Linux"
#else
#define BN_PLATFORM_NAME "Unknown"
#endif

#if defined(__APPLE__) || (defined(__linux__) && !defined(__ANDROID__))
#define BN_HAS_EXECINFO 1
#include <execinfo.h>
#else
#define BN_HAS_EXECINFO 0
#endif

#if defined(__ANDROID__)
#include <android/log.h>
#endif

#if defined(_MSC_VER)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <crtdbg.h>
#include <stdlib.h>
#include <io.h>
#include <wchar.h>
#else
#include <unistd.h>
#endif

namespace
{
    std::atomic<bool> s_installed{false};
    std::atomic<bool> s_initialized{false};
    std::atomic<bool> s_finishPrinted{false};
    std::atomic<int>  s_exitCode{0};

    // Process start time (zero until Initialize()).
    std::chrono::steady_clock::time_point s_startTime{};

    bool s_ansiEnabled{false};

    void WriteDiagnosticsMirror(const char* text)
    {
        if (text == nullptr || text[0] == '\0')
        {
            return;
        }

#if defined(_WIN32)
        ::OutputDebugStringA(text);
#endif

#if defined(__ANDROID__)
        __android_log_write(ANDROID_LOG_ERROR, "BabylonNative", text);
#endif
    }

    void WriteNativeCallstack(unsigned int skipFrames)
    {
#if BN_HAS_EXECINFO
        void* frames[64];
        const int frameCount = ::backtrace(frames, static_cast<int>(sizeof(frames) / sizeof(frames[0])));
        int firstFrame = static_cast<int>(2 + skipFrames);
        if (firstFrame > frameCount)
        {
            firstFrame = frameCount;
        }

        std::fprintf(stderr, "Callstack (%d):\n", frameCount - firstFrame);
        if (firstFrame < frameCount)
        {
            ::backtrace_symbols_fd(frames + firstFrame, frameCount - firstFrame, ::fileno(stderr));
        }
#else
        (void)skipFrames;
        std::fprintf(stderr, "Callstack: unavailable in this build.\n");
#endif
    }

#if defined(_MSC_VER)
    void __cdecl OnInvalidParameter(
        const wchar_t* expression,
        const wchar_t* function,
        const wchar_t* file,
        unsigned int line,
        uintptr_t /*reserved*/)
    {
        // Format wchar_t inputs into the message body via %ls.
        Diagnostics::DumpFailure(
            "INVALID PARAMETER",
            nullptr,
            0,
            1 /* skip self */,
            "function=%ls expression=%ls (%ls:%u)",
            function != nullptr ? function : L"(null)",
            expression != nullptr ? expression : L"(null)",
            file != nullptr ? file : L"(null)",
            line);

        if (::IsDebuggerPresent())
        {
            __debugbreak();
        }
        Diagnostics::SetExitCode(3);
        Diagnostics::PrintFinishLine();
        std::_Exit(3);
    }

    void OnSignalAbort(int /*signal*/)
    {
        Diagnostics::DumpFailure("ABORT", nullptr, 0, 1, "SIGABRT raised.");
        if (::IsDebuggerPresent())
        {
            __debugbreak();
        }
        Diagnostics::SetExitCode(3);
        Diagnostics::PrintFinishLine();
        std::_Exit(3);
    }

    int OnCrtReport(int reportType, char* message, int* returnValue)
    {
        const char* kind = (reportType == _CRT_WARN)   ? "CRT WARN"
                         : (reportType == _CRT_ERROR)  ? "CRT ERROR"
                         : (reportType == _CRT_ASSERT) ? "CRT ASSERT"
                                                       : "CRT UNKNOWN";
        Diagnostics::DumpFailure(
            kind, nullptr, 0, 1,
            "%s",
            message != nullptr ? message : "(null)");

        if (returnValue != nullptr)
        {
            // Returning 1 here would trap into __debugbreak(); avoid that on
            // a no-debugger run -- it raises EXCEPTION_BREAKPOINT and exits
            // with STATUS_BREAKPOINT instead of our chosen exit code.
            *returnValue = ::IsDebuggerPresent() ? 1 : 0;
        }
        // TRUE suppresses the modal dialog.
        return TRUE;
    }
#else
    void OnSignalAbort(int /*signal*/)
    {
        Diagnostics::DumpFailure("ABORT", nullptr, 0, 1, "SIGABRT raised.");
        Diagnostics::SetExitCode(3);
        Diagnostics::PrintFinishLine();
        std::_Exit(3);
    }
#endif
}

namespace Diagnostics
{
    void InstallCrashHandler()
    {
        bool expected = false;
        if (!s_installed.compare_exchange_strong(expected, true))
        {
            return;
        }

#if defined(_MSC_VER)
        // Route assert() to stderr instead of UCRT's modal dialog. Covers the
        // direct assert() codepath; _CrtSetReportMode below covers _CRT_*.
        _set_error_mode(_OUT_TO_STDERR);

        // Disable abort()'s retry/ignore message box.
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

        _set_invalid_parameter_handler(&OnInvalidParameter);
        std::signal(SIGABRT, &OnSignalAbort);

#if defined(_DEBUG)
        // Force CRT report output to stderr and through our hook (debug CRT only).
        const int kReportTypes[] = {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT};
        for (int reportType : kReportTypes)
        {
            _CrtSetReportMode(reportType, _CRTDBG_MODE_FILE);
            _CrtSetReportFile(reportType, _CRTDBG_FILE_STDERR);
        }
        _CrtSetReportHook(&OnCrtReport);
#endif
#else
        std::signal(SIGABRT, &OnSignalAbort);
#endif
    }

    void Initialize()
    {
        bool expected = false;
        if (!s_initialized.compare_exchange_strong(expected, true))
        {
            return;
        }

        s_startTime = std::chrono::steady_clock::now();

#if defined(_MSC_VER)
        // Enable ANSI VT processing so the colored finish line renders.
        const HANDLE hOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOut != INVALID_HANDLE_VALUE && hOut != nullptr)
        {
            DWORD mode = 0;
            if (::GetConsoleMode(hOut, &mode))
            {
                if (::SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
                {
                    s_ansiEnabled = true;
                }
            }
        }

        // Don't emit ANSI escapes when stdout is redirected to a file/pipe.
        if (!::_isatty(::_fileno(stdout)))
        {
            s_ansiEnabled = false;
        }
#else
        s_ansiEnabled = ::isatty(::fileno(stdout)) != 0;
#endif

        InstallCrashHandler();

        // Print finish line on every exit path. Both handlers idempotent
        // via s_finishPrinted; whichever fires first wins.
        std::atexit(&PrintFinishLine);
        std::at_quick_exit(&PrintFinishLine);
    }

    void SetExitCode(int code)
    {
        s_exitCode.store(code, std::memory_order_relaxed);
    }

    void PrintFinishLine()
    {
        bool expected = false;
        if (!s_finishPrinted.compare_exchange_strong(expected, true))
        {
            return;
        }

        const int code = s_exitCode.load(std::memory_order_relaxed);
        const bool success = (code == 0);

        const auto elapsed = std::chrono::steady_clock::now() - s_startTime;
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

        // Format elapsed as "1m 23.456s" / "12.345s" / "1234 ms".
        char timeBuf[64];
        if (ms >= 60000)
        {
            const long long totalSec = ms / 1000;
            const long long minutes = totalSec / 60;
            const long long seconds = totalSec % 60;
            const long long millis = ms % 1000;
            std::snprintf(timeBuf, sizeof(timeBuf), "%lldm %lld.%03llds", minutes, seconds, millis);
        }
        else if (ms >= 1000)
        {
            const long long seconds = ms / 1000;
            const long long millis = ms % 1000;
            std::snprintf(timeBuf, sizeof(timeBuf), "%lld.%03llds", seconds, millis);
        }
        else
        {
            std::snprintf(timeBuf, sizeof(timeBuf), "%lld ms", static_cast<long long>(ms));
        }

        const char* colorOn  = "";
        const char* colorOff = "";
        if (s_ansiEnabled)
        {
            colorOn  = success ? "\x1b[1;32m" : "\x1b[1;31m";  // bold green / bold red
            colorOff = "\x1b[0m";
        }

        // fputs via stdio (unbuffered, set in wWinMain) so the line reaches
        // the pipe before any subsequent _Exit / quick_exit.
        std::fprintf(stdout,
                     "%sPlayground: Finished in %s. (exit %d)%s\n",
                     colorOn, timeBuf, code, colorOff);
        std::fflush(stdout);
    }

    void DumpFailureV(const char* category, const char* file, int line, unsigned int skipFrames, const char* fmt, va_list args)
    {
        std::fprintf(stderr, "\n--- BN: %s ---\n\n", category != nullptr ? category : "FAILURE");

        if (file != nullptr)
        {
            std::fprintf(stderr, "%s(%d): ", file, line);
        }

        if (fmt != nullptr)
        {
            char message[8 * 1024];
            va_list argsCopy;
            va_copy(argsCopy, args);
            const int written = std::vsnprintf(message, sizeof(message), fmt, argsCopy);
            va_end(argsCopy);

            if (written >= 0)
            {
                std::fputs(message, stderr);
                WriteDiagnosticsMirror(message);
            }
            else
            {
                std::fputs("(failed to format diagnostic message)", stderr);
            }
        }

        std::fputs("\n\n", stderr);

        // +2 skips this helper and the public DumpFailure trampoline.
        WriteNativeCallstack(skipFrames);

        std::fprintf(stderr,
            "\nBuild info:\n"
            "\tCompiler: %s, CPU: %s, OS: %s, C++: %s, Date: %s, Time: %s\n"
            "\n--- END ---\n\n",
            BN_COMPILER_NAME,
            BN_CPU_NAME,
            BN_PLATFORM_NAME,
            BN_STRINGIFY(__cplusplus),
            __DATE__,
            __TIME__);
        std::fflush(stderr);
    }

    void DumpFailure(const char* category, const char* file, int line, unsigned int skipFrames, const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        DumpFailureV(category, file, line, skipFrames, fmt, args);
        va_end(args);
    }
}
