#include "network/Wakeup.h"

#include <cstring>

Wakeup::~Wakeup()
{
    close();
}

bool Wakeup::init(std::string &errorMessage)
{
    _socket = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (_socket == kInvalidSocket)
    {
        errorMessage = "创建唤醒器 socket 失败:" + SocketOps::lastErrorText();
        return false;
    }

    sockaddr_in local = {};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 只在本机回环，不暴露到网卡
    local.sin_port = 0;                             // 让内核分配端口

    if (::bind(_socket, reinterpret_cast<sockaddr *>(&local), sizeof(local)) != 0)
    {
        errorMessage = "绑定唤醒器到回环地址失败:" + SocketOps::lastErrorText();
        close();
        return false;
    }

    // 端口是内核给的，必须问回来才知道往哪投递
#ifdef _WIN32
    int addressLength = sizeof(_address);
#else
    socklen_t addressLength = sizeof(_address);
#endif
    if (::getsockname(_socket, reinterpret_cast<sockaddr *>(&_address), &addressLength) != 0)
    {
        errorMessage = "获取唤醒器地址失败:" + SocketOps::lastErrorText();
        close();
        return false;
    }

    if (!SocketOps::setNonBlocking(_socket))
    {
        errorMessage = "把唤醒器设为非阻塞失败:" + SocketOps::lastErrorText();
        close();
        return false;
    }

    errorMessage.clear();
    return true;
}

void Wakeup::notify()
{
    if (_socket == kInvalidSocket)
    {
        return;
    }

    // 内容无意义，收到的字节一律丢弃：信号量是「有/没有」，不是「有几条消息」
    const char signal = 'w';
    ::sendto(
        _socket,
        &signal,
        1,
        0,
        reinterpret_cast<const sockaddr *>(&_address),
        sizeof(_address));
}

void Wakeup::drain()
{
    if (_socket == kInvalidSocket)
    {
        return;
    }

    char buffer[64];
    while (true)
    {
        const int receivedBytes = ::recv(_socket, buffer, sizeof(buffer), 0);
        if (receivedBytes <= 0)
        {
            // wouldBlock：排空了，正常返回。
            // 0 或其它错误：UDP 上不会是「对端关闭」，一律当作排空处理。
            return;
        }
    }
}

void Wakeup::close()
{
    if (_socket == kInvalidSocket)
    {
        return;
    }
    SocketOps::close(_socket);
    _socket = kInvalidSocket;
}
