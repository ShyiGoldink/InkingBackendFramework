#ifndef INKING_BACKEND_FRAMEWORK_PROTOCOL_PROTOCOL_FRAME_H
#define INKING_BACKEND_FRAMEWORK_PROTOCOL_PROTOCOL_FRAME_H

#include <cstddef>
#include <cstdint>
#include <string>

/**
 * @brief 协议帧结构与帧头常量。
 *
 * 帧的布局（所有整数字段都使用网络字节序，也就是大端）：
 *
 *   +------------------+-------------------+------------------+----------------+
 *   | 包长（8 字节）    | 指令长（4 字节）   | 指令（UTF-8）     | 数据（变长）   |
 *   +------------------+-------------------+------------------+----------------+
 *
 * - 包长：整帧的总字节数，包含帧头、指令和数据本身；
 * - 指令长：指令部分的字节数，数据长度 = 包长 - 帧头长度 - 指令长；
 * - 数据：上层消息的字节（按约定是 protobuf 序列化结果）。
 *
 * 帧层只负责在字节流里找出「一帧」的边界，不解释指令含义，也不依赖 protobuf。
 */
struct CommandFrame
{
    std::string command; /** 指令，UTF-8 文本 */
    std::string payload; /** 数据，按约定是 protobuf 序列化后的字节 */
};

namespace ProtocolFrame
{
    /** 包长字段占用的字节数 */
    inline constexpr std::size_t kPacketLengthBytes = 8;
    /** 指令长字段占用的字节数 */
    inline constexpr std::size_t kCommandLengthBytes = 4;
    /** 帧头总长度 */
    inline constexpr std::size_t kHeaderBytes = kPacketLengthBytes + kCommandLengthBytes;

    /** 指令长度上限：指令是短文本，超过这个长度按非法帧处理 */
    inline constexpr std::size_t kMaxCommandBytes = 1024;
    /**
     * 整帧长度上限：防止对端报出一个荒唐的长度把内存撑爆。
     * 有了这个上限，未解析缓冲最多只会到「一帧」的大小；
     * 对端一直不补齐由上层（比如网络层）用超时处理，帧层不掺和这件事。
     */
    inline constexpr std::size_t kMaxFrameBytes = 16 * 1024 * 1024;
}

#endif // INKING_BACKEND_FRAMEWORK_PROTOCOL_PROTOCOL_FRAME_H
