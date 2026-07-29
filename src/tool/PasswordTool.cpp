#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "tool/PasswordTool.h"

namespace
{
    uint64_t mix(uint64_t value)
    {
        value ^= value >> 30;
        value *= 0xbf58476d1ce4e5b9ULL;
        value ^= value >> 27;
        value *= 0x94d049bb133111ebULL;
        value ^= value >> 31;
        return value;
    }

    std::string toHex(uint64_t value)
    {
        std::ostringstream stream;
        stream << std::hex << std::setw(16) << std::setfill('0') << value;
        return stream.str();
    }
}

bool PasswordTool::verifyConsole() const
{ // 最大密码输入次数
    constexpr int maxAttempts = 5;
    // 进行n次密码输入尝试
    for (int attempt = 1; attempt <= maxAttempts; attempt++)
    {
        std::string password;
        std::cout << "\033[36m请输入密码: \033[0m";
        // 如果控制台输入错误，则返回密码输入失败，直接返回false
        if (!std::getline(std::cin, password))
        {
            std::cout << "\033[38;5;208m密码输入失败。\033[0m" << std::endl;
            return false;
        }
        // 如果密码正确，那么直接返回true
        if (verify(password))
        {
            return true;
        }
        std::cout << "\033[38;5;208m密码错误。\033[0m" << std::endl;
    }
    std::cout << "\033[31m密码错误次数过多。\033[0m" << std::endl;
    return false;
}

bool PasswordTool::verify(const std::string &password) const
{
    const std::string actualHash = deriveHash(password);
    return fixedTimeEquals(actualHash, std::string(kExpectedHash));
}

std::string PasswordTool::deriveHash(const std::string &password) const
{
    uint64_t stateA = 0x123456789abcdef0ULL;
    uint64_t stateB = 0xfedcba9876543210ULL;

    const std::string data =
        std::string(kSaltPart1) + password + std::string(kSaltPart2);

    for (int round = 0; round < 50000; ++round)
    {
        for (unsigned char ch : data)
        {
            stateA = mix(stateA ^ ch ^ static_cast<uint64_t>(round));
            stateB = mix(stateB + ch + stateA);
        }

        stateA = mix(stateA ^ stateB ^ static_cast<uint64_t>(round));
        stateB = mix(stateB + stateA + 0x9e3779b97f4a7c15ULL);
    }

    return toHex(stateA) + toHex(stateB);
}

bool PasswordTool::fixedTimeEquals(const std::string &left, const std::string &right) const
{
    if (left.size() != right.size())
    {
        return false;
    }

    unsigned char diff = 0;
    for (std::size_t i = 0; i < left.size(); ++i)
    {
        diff |= static_cast<unsigned char>(left[i] ^ right[i]);
    }

    return diff == 0;
}