#ifndef INKING_BACKEND_FRAMEWORK_NET_NET_MESSAGE_H
#define INKING_BACKEND_FRAMEWORK_NET_NET_MESSAGE_H

#include <string>

/**
 * @brief 网络消息（纯数据结构）。
 *
 * 这是 TcpServer 与业务层之间传递的统一消息结构。
 *
 *   协议格式：指令/数据
 *   - command : 指令（如 GET_TIME / GET_DATA_MINUTE / SEND_SENSE_DATA）
 *   - data    : 数据（可以为空，上传时为 "温度,湿度"）
 *
 * 传输方式：
 *   整条消息按 UTF-8 文本发送，以 '\n' 结尾；
 *   换行符用于拆分 TCP 粘连的多个消息，因此消息内部不允许出现 '\n'。
 */
struct NetMessage
{
    std::string command;  /** 指令 */
    std::string data;     /** 数据 */
};

#endif // INKING_BACKEND_FRAMEWORK_NET_NET_MESSAGE_H
