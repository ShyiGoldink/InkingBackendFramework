#ifndef INKING_BACKEND_FRAMEWORK_NET_SIMPLE_TEXT_PROTOCOL_H
#define INKING_BACKEND_FRAMEWORK_NET_SIMPLE_TEXT_PROTOCOL_H

#include "net/IProtocol.h"

/**
 * @brief 文本协议：指令/数据。
 *
 * 示例：GET_TIME/\n
 *       GET_DATA_MINUTE/\n
 *       SEND_SENSE_DATA/25.50,60.00\n
 *
 * 注意：
 * - 按第一个 '/' 切分，command 不允许为空；data 可以为空；
 * - 指令名与 data 内部不允许出现换行符（换行是消息边界）。
 */
class SimpleTextProtocol final : public IProtocol
{
public:
    bool decode(const std::string &line, NetMessage &message) const override;
    std::string encode(const NetMessage &message) const override;
};

#endif // INKING_BACKEND_FRAMEWORK_NET_SIMPLE_TEXT_PROTOCOL_H
