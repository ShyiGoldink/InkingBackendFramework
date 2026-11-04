#include "core/ConsoleEnvironment.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace
{
#ifdef _WIN32
    /**
     * Windows 上不能用 _isatty 判断"是不是终端"：
     * NUL 设备（即 /dev/null）的 GetFileType 也是 FILE_TYPE_CHAR，_isatty 会返回真，
     * 于是 `--start < /dev/null` 会被误判成"有人坐在控制台前"，又回去要求输密码。
     * 可靠的判断只有 GetConsoleMode：只有真正的控制台句柄才拿得到模式。
     */
    bool isConsoleHandle(DWORD stdHandleId)
    {
        const HANDLE handle = ::GetStdHandle(stdHandleId);
        if (handle == nullptr || handle == INVALID_HANDLE_VALUE)
        {
            return false;
        }
        DWORD mode = 0;
        return ::GetConsoleMode(handle, &mode) != 0;
    }
#endif
}

bool ConsoleEnvironment::stdinIsTerminal()
{
#ifdef _WIN32
    return isConsoleHandle(STD_INPUT_HANDLE);
#else
    return ::isatty(STDIN_FILENO) != 0;
#endif
}

bool ConsoleEnvironment::stdoutIsTerminal()
{
#ifdef _WIN32
    return isConsoleHandle(STD_OUTPUT_HANDLE);
#else
    return ::isatty(STDOUT_FILENO) != 0;
#endif
}
