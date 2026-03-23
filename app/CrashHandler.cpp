#include "CrashHandler.h"
#include "Logger.h"

#include <filesystem>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>

std::string CrashHandler::s_dumpDir;

// ── 时间戳工具 ─────────────────────────────────────────────────────────────
static std::string timestampStr()
{
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return oss.str();
}

// ══════════════════════════════════════════════════════════════════════════
//  Windows 实现
// ══════════════════════════════════════════════════════════════════════════
#if defined(PLATFORM_WINDOWS)
#include <windows.h>
#include <dbghelp.h>

void CrashHandler::install(const std::string& dumpDir)
{
    s_dumpDir = dumpDir;
    std::filesystem::create_directories(dumpDir);
    SetUnhandledExceptionFilter(reinterpret_cast<LPTOP_LEVEL_EXCEPTION_FILTER>(onUnhandledException));
    LOG_INFO("CrashHandler installed (Windows MiniDump) → {}", dumpDir);
}

long __stdcall CrashHandler::onUnhandledException(void* exceptionInfo)
{
    std::string path = s_dumpDir + "/crash_" + timestampStr() + ".dmp";
    LOG_CRITICAL("Unhandled exception! Writing dump → {}", path);
    Logger::instance().shutdown();   // 确保日志落盘

    writeMiniDump(exceptionInfo, path);
    return EXCEPTION_EXECUTE_HANDLER;
}

void CrashHandler::writeMiniDump(void* exceptionInfo, const std::string& path)
{
    HANDLE hFile = CreateFileA(
        path.c_str(),
        GENERIC_WRITE,
        0, nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (hFile == INVALID_HANDLE_VALUE) return;

    MINIDUMP_EXCEPTION_INFORMATION mei{};
    mei.ThreadId          = GetCurrentThreadId();
    mei.ExceptionPointers = static_cast<PEXCEPTION_POINTERS>(exceptionInfo);
    mei.ClientPointers    = FALSE;

    MiniDumpWriteDump(
        GetCurrentProcess(),
        GetCurrentProcessId(),
        hFile,
        static_cast<MINIDUMP_TYPE>(
            MiniDumpWithDataSegs      |
            MiniDumpWithHandleData    |
            MiniDumpWithThreadInfo    |
            MiniDumpWithFullMemoryInfo
        ),
        exceptionInfo ? &mei : nullptr,
        nullptr,
        nullptr
    );
    CloseHandle(hFile);
}

// ══════════════════════════════════════════════════════════════════════════
//  Linux 实现
// ══════════════════════════════════════════════════════════════════════════
#elif defined(PLATFORM_LINUX)
#include <signal.h>
#include <execinfo.h>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <array>

static struct sigaction s_oldSigActions[NSIG];

static void signalHandler(int sig, siginfo_t* info, void* ucontext)
{
    std::string path = CrashHandler::s_dumpDir +
                       "/crash_" + timestampStr() + ".log";

    LOG_CRITICAL("Signal {} received! Writing backtrace → {}", sig, path);
    Logger::instance().shutdown();

    // backtrace
    std::array<void*, 64> frames{};
    int count = backtrace(frames.data(), static_cast<int>(frames.size()));
    char** symbols = backtrace_symbols(frames.data(), count);

    std::ofstream ofs(path);
    ofs << "Signal: " << sig << " (" << strsignal(sig) << ")\n";
    ofs << "Backtrace (" << count << " frames):\n";
    for (int i = 0; i < count && symbols; ++i) {
        ofs << "  [" << i << "] " << symbols[i] << "\n";
    }
    free(symbols);
    ofs.close();

    // 调用默认处理器（生成 core dump）
    struct sigaction& old = s_oldSigActions[sig];
    if (old.sa_flags & SA_SIGINFO) {
        old.sa_sigaction(sig, info, ucontext);
    } else if (old.sa_handler != SIG_DFL && old.sa_handler != SIG_IGN) {
        old.sa_handler(sig);
    } else {
        signal(sig, SIG_DFL);
        raise(sig);
    }
}

void CrashHandler::install(const std::string& dumpDir)
{
    s_dumpDir = dumpDir;
    std::filesystem::create_directories(dumpDir);

    struct sigaction sa{};
    sa.sa_sigaction = signalHandler;
    sa.sa_flags     = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);

    for (int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS}) {
        sigaction(sig, &sa, &s_oldSigActions[sig]);
    }
    LOG_INFO("CrashHandler installed (Linux signal+backtrace) → {}", dumpDir);
}

#else
// ── 其他平台：空实现 ────────────────────────────────────────────────────
void CrashHandler::install(const std::string& dumpDir)
{
    s_dumpDir = dumpDir;
    LOG_WARN("CrashHandler: no implementation for this platform");
}
#endif
