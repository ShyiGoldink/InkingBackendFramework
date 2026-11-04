#ifndef INKING_BACKEND_FRAMEWORK_PROTOCOL_FRAME_CODEC_H
#define INKING_BACKEND_FRAMEWORK_PROTOCOL_FRAME_CODEC_H

#include "protocol/ProtocolFrame.h"

#include <cstddef>
#include <string>

/**
 * @brief 协议帧的封装与解析。
 *
 * 负责三件事：
 *
 * 1. 组帧：把一条 CommandFrame 编码成可以直接发送的字节；
 * 2. 拆帧：把收到的字节按顺序喂进来，取出一帧一帧的完整数据；
 * 3. 处理粘包与半包：TCP 一次收到的内容可能含有多帧，也可能只有半帧，
 *    这些边界判断全部留在本类里，调用方只需要关心「收到一帧」。
 *
 * 出现非法帧（魔数不对、包长越界、指令长超出包长）时丢弃当前缓冲并返回
 * InvalidFrame。**帧层不做重同步**：不会在字节流里扫描魔数试图找回边界，
 * 因为数据字段是任意字节，扫描出来的「帧」并不可信（完整理由见 ProtocolFrame.h）。
 *
 * 调用方应当把 InvalidFrame 当作「这条连接已经不可信」的信号：在连接层关闭它。
 * 在同一条流上继续解析没有意义——错位一旦发生，后面每一帧都会跟着错。
 */
class FrameCodec
{
public:
    enum class Status
    {
        Ok,           /** 成功取出一帧 */
        NeedMoreData, /** 还没有凑够一整帧，等待后续数据 */
        InvalidFrame, /** 帧头不合法（长度越界或指令长超过包长）；缓冲已丢弃 */
    };

    FrameCodec() = default;

    /**
     * @brief 编码一条消息。
     * @param frame 待发送的消息
     * @param out 成功时写入整帧字节；失败时不修改 out
     * @return 成功返回 Ok；指令过长或整帧过大返回 InvalidFrame
     */
    static Status encode(const CommandFrame &frame, std::string &out);

    /**
     * @brief 解析一整帧。
     * @param data 起始地址
     * @param size 字节数，必须正好是一帧，多一个字节也算不合法
     * @param frame 成功时写入解析结果
     */
    static Status decode(const char *data, std::size_t size, CommandFrame &frame);
    static Status decode(const std::string &data, CommandFrame &frame);

    /** 追加收到的字节（线程不安全，每条连接各持有一个实例即可） */
    void push(const char *data, std::size_t size);
    void push(const std::string &data);

    /**
     * @brief 取出一帧。
     * @param frame 返回 Ok 时写入解析结果
     * @return Ok 表示取到完整一帧；NeedMoreData 表示还需要更多数据；
     *         InvalidFrame 表示帧头不合法，缓冲区已被丢弃
     */
    Status next(CommandFrame &frame);

    /** 还没有解析的字节数 */
    std::size_t pendingBytes() const;

    /** 丢弃尚未解析的字节，用于出错后重新同步 */
    void reset();

    /** 状态名，便于日志输出 */
    static const char *statusName(Status status);

private:
    std::string _buffer;       /** 接收缓冲：尚未取走的字节 */
    std::size_t _consumed = 0; /** _buffer 中已经被取走的前缀长度 */
};

#endif // INKING_BACKEND_FRAMEWORK_PROTOCOL_FRAME_CODEC_H
