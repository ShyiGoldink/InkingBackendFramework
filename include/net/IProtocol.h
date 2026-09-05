#ifndef INKING_BACKEND_FRAMEWORK_NET_IPROTOCOL_H
#define INKING_BACKEND_FRAMEWORK_NET_IPROTOCOL_H

#include "net/NetMessage.h"

#include <string>

/**
 * @brief 网络协议接口（抽象层）。
 *
 * TcpServer 只负责按 '\n' 拆分 TCP 字节流，
 * “一行文本 <-> 业务消息”的转换全部交给 IProtocol 的实现。
 * 确认后的文本格式为“指令/数据”，由 SimpleTextProtocol 实现；
 * 未来需要调整时新增/替换协议实现即可，不需要改动 TcpServer 的拆包逻辑。
 */
class IProtocol
{
public:
    virtual ~IProtocol() = default;

    /**
     * @brief 把一行协议文本解析成业务消息。
     * @param line 不包含结尾 '\n' 的一行文本。
     * @param message 解析成功时输出业务消息。
     * @return true 解析成功；false 格式不合法（TcpServer 会记录日志并忽略）。
     */
    virtual bool decode(const std::string &line, NetMessage &message) const = 0;

    /**
     * @brief 把业务消息编码成一行协议文本。
     * @param message 需要发送的业务消息。
     * @return 不包含结尾 '\n' 的协议文本；由 TcpServer 负责追加换行。
     */
    virtual std::string encode(const NetMessage &message) const = 0;
};

#endif // INKING_BACKEND_FRAMEWORK_NET_IPROTOCOL_H
