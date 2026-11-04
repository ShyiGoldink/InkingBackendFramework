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
 *   +----------+----------+----------+------------------+------------------+
 *   | 魔数     | 包长     | 指令长   | 指令（UTF-8）    | 数据（变长）     |
 *   | 2 字节   | 4 字节   | 2 字节   | 变长             |                  |
 *   +----------+----------+----------+------------------+------------------+
 *
 * - 魔数：固定常量 `kMagic`，用来判定「这是不是本协议的数据」。它排在最前面，
 *   因为只需要 2 个字节就能作出判断，非本协议的流量能在最短路径上被挡住。
 * - 包长：整帧的总字节数，**包含魔数在内**。从魔数第一个字节算起，到数据最后
 *   一个字节为止。整个帧格式只有这一个不变量：
 *
 *       包长 == 这一帧占用的全部字节数
 *
 *   于是「下一帧从哪里开始」永远是「当前帧起点 + 包长」，不需要任何偏移修正。
 * - 指令长：指令部分的字节数。数据长度是派生值，不占字段：
 *
 *       数据长度 = 包长 - 帧头（8） - 指令长
 *
 * - 数据：上层消息的字节（按约定是 protobuf 序列化结果）。
 *
 * 帧层只负责在字节流里找出「一帧」的边界，不解释指令含义，也不依赖 protobuf。
 *
 * ## 关于「不做重同步」这个决定
 *
 * 帧头不合法时，FrameCodec 直接丢弃缓冲并返回 InvalidFrame，**不会在字节流里
 * 扫描魔数试图找回边界**。三条理由：
 *
 * 1. TCP 保证字节流的完整与有序，所以分帧错位只可能来自两种原因——本端代码有
 *    bug，或者对端在乱发。前者该修代码，后者该断连接，都不是靠扫描能解决的。
 * 2. 数据字段是 protobuf 的任意字节，魔数完全可能出现在数据内部。扫描一旦命中
 *    数据里的假魔数，后面所有帧都会错位，而且越错越远。
 * 3. 业界在可靠传输上也是这么做的：TLS、HTTP/2、gRPC、Kafka 的帧头都没有魔数，
 *    解码失败一律终止连接。
 *
 * 所以防守责任不在帧层而在连接层：魔数不对、半包超时、帧速率超限，这三条都应该
 * 由连接层决定「断开这条连接」；帧层只负责如实报告解析结果。
 */
struct CommandFrame
{
    std::string command; /** 指令，UTF-8 文本 */
    std::string payload; /** 数据，按约定是 protobuf 序列化后的字节 */
};

namespace ProtocolFrame
{
    /** 魔数：固定值，用于识别帧起点、筛掉非本协议的流量 */
    inline constexpr std::uint16_t kMagic = 0xABCD;
    /** 魔数字段占用的字节数 */
    inline constexpr std::size_t kMagicBytes = 2;
    /** 包长字段占用的字节数（4 字节足够：整帧上限见 kMaxFrameBytes） */
    inline constexpr std::size_t kPacketLengthBytes = 4;
    /** 指令长字段占用的字节数 */
    inline constexpr std::size_t kCommandLengthBytes = 2;
    /** 帧头总长度：魔数 + 包长 + 指令长 = 8 字节 */
    inline constexpr std::size_t kHeaderBytes =
        kMagicBytes + kPacketLengthBytes + kCommandLengthBytes;

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
