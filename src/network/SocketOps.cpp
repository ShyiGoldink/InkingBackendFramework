#include "network/SocketOps.h"

#include <system_error>

#if defined(__linux__)
#include <dirent.h>
#endif

namespace SocketOps
{
    bool startUp(std::string &errorMessage)
    {
#ifdef _WIN32
        WSADATA wsaData = {};
        const int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0)
        {
            std::error_code code(result, std::system_category());
            errorMessage = "网络服务请求失败:" + code.message();
            return false;
        }
        return true;
#else
        (void)errorMessage;
        return true;
#endif
    }

    void cleanUp()
    {
#ifdef _WIN32
        WSACleanup();
#endif
    }

    bool isValid(SocketType socket)
    {
        return socket != kInvalidSocket;
    }

    bool setNonBlocking(SocketType socket)
    {
#ifdef _WIN32
        u_long mode = 1;
        return ioctlsocket(socket, FIONBIO, &mode) == 0;
#else
        const int flags = fcntl(socket, F_GETFL, 0);
        if (flags == -1)
        {
            // 取不到原标志时不要盲目 F_SETFL：那会把已有的 O_CLOEXEC 之类的位一起抹掉
            return false;
        }
        return fcntl(socket, F_SETFL, flags | O_NONBLOCK) != -1;
#endif
    }

    bool setReuseAddress(SocketType socket)
    {
        const int enabled = 1;
#ifdef _WIN32
        const char *value = reinterpret_cast<const char *>(&enabled);
#else
        const void *value = &enabled;
#endif
        return setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, value, sizeof(enabled)) == 0;
    }

    bool setNoDelay(SocketType socket)
    {
        const int enabled = 1;
#ifdef _WIN32
        const char *value = reinterpret_cast<const char *>(&enabled);
#else
        const void *value = &enabled;
#endif
        return setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, value, sizeof(enabled)) == 0;
    }

    bool wouldBlock()
    {
#ifdef _WIN32
        return WSAGetLastError() == WSAEWOULDBLOCK;
#else
        return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
    }

    std::string lastErrorText()
    {
#ifdef _WIN32
        std::error_code code(WSAGetLastError(), std::system_category());
#else
        std::error_code code(errno, std::system_category());
#endif
        return code.message();
    }

    int sendFlags()
    {
#ifdef MSG_NOSIGNAL
        return MSG_NOSIGNAL;
#else
        return 0;
#endif
    }

    void close(SocketType socket)
    {
        if (!isValid(socket))
        {
            return;
        }
#ifdef _WIN32
        closesocket(socket);
#else
        ::close(socket);
#endif
    }

    void shutdownWrite(SocketType socket)
    {
        if (!isValid(socket))
        {
            return;
        }
#ifdef _WIN32
        ::shutdown(socket, SD_SEND);
#else
        ::shutdown(socket, SHUT_WR);
#endif
    }

    std::size_t openHandleCount()
    {
#ifdef _WIN32
        DWORD count = 0;
        if (!::GetProcessHandleCount(::GetCurrentProcess(), &count))
        {
            return 0;
        }
        return static_cast<std::size_t>(count);
#elif defined(__linux__)
        // Linux：数 /proc/self/fd 下的条目。opendir 自己也会占一个 fd，
        // 所以减 1 才是「调用前」的真实值。
        DIR *dir = ::opendir("/proc/self/fd");
        if (dir == nullptr)
        {
            return 0;
        }
        std::size_t count = 0;
        while (::readdir(dir) != nullptr)
        {
            ++count;
        }
        ::closedir(dir);
        // "." 与 ".." 各占一个
        return count > 2 ? count - 3 : 0;
#else
        return 0; // 其它平台不支持：返回 0 表示「没量到」，不是「零个」
#endif
    }
}
