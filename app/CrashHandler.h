#pragma once

#include <string>

/**
 * @brief 崩溃处理器
 *
 * Windows: SetUnhandledExceptionFilter + MiniDumpWriteDump
 * Linux/Mac: signal(SIGSEGV/SIGABRT/SIGFPE) + backtrace 写文件
 *
 * 调用 CrashHandler::install() 后，程序崩溃时会在 dumpDir 目录下
 * 生成带时间戳的 dump 文件并记录日志。
 */
class CrashHandler
{
public:
    static void install(const std::string& dumpDir = "dumps");

    // public 供平台信号处理函数（文件级静态函数）访问
    static std::string s_dumpDir;

private:
    CrashHandler() = delete;

#if defined(PLATFORM_WINDOWS)
    static long __stdcall onUnhandledException(void* exceptionInfo);
    static void writeMiniDump(void* exceptionInfo, const std::string& path);
#endif
};
