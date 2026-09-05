#include "net/SimpleTextProtocol.h"

bool SimpleTextProtocol::decode(const std::string &line, NetMessage &message) const
{
    // 按第一个 '/' 切分：指令/数据
    const std::size_t separator = line.find('/');
    if (separator == std::string::npos)
    {
        // 容错：没有 '/' 时整行视为指令，数据为空
        message.command = line;
        message.data.clear();
        return !line.empty();
    }
    if (separator == 0)
    {
        return false;
    }

    message.command = line.substr(0, separator);
    message.data = line.substr(separator + 1);
    return true;
}

std::string SimpleTextProtocol::encode(const NetMessage &message) const
{
    // 编码与 decode 保持对称：指令/数据
    return message.command + "/" + message.data;
}
