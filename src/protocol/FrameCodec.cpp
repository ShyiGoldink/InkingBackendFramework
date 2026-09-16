#include "protocol/FrameCodec.h"

#include <cstdint>

namespace
{
    /** 消费过的前缀超过这个长度就把缓冲前移，避免缓冲区一直向右增长 */
    constexpr std::size_t kCompactThreshold = 4096;

    /** 按大端把整数写入字段，bytes 为字段宽度 */
    void writeBigEndian(std::string &out, std::uint64_t value, std::size_t bytes)
    {
        for (std::size_t i = 0; i < bytes; ++i)
        {
            const std::size_t shift = (bytes - 1 - i) * 8;
            out.push_back(static_cast<char>((value >> shift) & 0xFFU));
        }
    }

    /** 按大端从字节流读出整数，bytes 为字段宽度 */
    std::uint64_t readBigEndian(const char *data, std::size_t bytes)
    {
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < bytes; ++i)
        {
            value = (value << 8) |
                    static_cast<std::uint64_t>(static_cast<unsigned char>(data[i]));
        }
        return value;
    }

    /**
     * @brief 尝试从 [data, data + available) 中解析出一帧。
     * @param frameSize 解析成功时写入整帧长度
     */
    FrameCodec::Status parseFrame(const char *data, std::size_t available,
                                  std::size_t &frameSize, CommandFrame &frame)
    {
        if (available < ProtocolFrame::kHeaderBytes)
        {
            return FrameCodec::Status::NeedMoreData;
        }

        const std::uint64_t packetBytes =
            readBigEndian(data, ProtocolFrame::kPacketLengthBytes);
        const std::uint64_t commandBytes =
            readBigEndian(data + ProtocolFrame::kPacketLengthBytes,
                          ProtocolFrame::kCommandLengthBytes);

        // 包长至少要装得下帧头，且不能超过上限；
        // 指令长既不能超过上限，也不能超出包长允许的范围
        if (packetBytes < ProtocolFrame::kHeaderBytes ||
            packetBytes > ProtocolFrame::kMaxFrameBytes ||
            commandBytes > ProtocolFrame::kMaxCommandBytes ||
            commandBytes > packetBytes - ProtocolFrame::kHeaderBytes)
        {
            return FrameCodec::Status::InvalidFrame;
        }

        frameSize = static_cast<std::size_t>(packetBytes);
        if (available < frameSize)
        {
            return FrameCodec::Status::NeedMoreData;
        }

        const std::size_t commandSize = static_cast<std::size_t>(commandBytes);
        frame.command.assign(data + ProtocolFrame::kHeaderBytes, commandSize);
        frame.payload.assign(data + ProtocolFrame::kHeaderBytes + commandSize,
                             frameSize - ProtocolFrame::kHeaderBytes - commandSize);
        return FrameCodec::Status::Ok;
    }
}

FrameCodec::Status FrameCodec::encode(const CommandFrame &frame, std::string &out)
{
    const std::size_t commandSize = frame.command.size();
    const std::size_t payloadSize = frame.payload.size();

    if (commandSize > ProtocolFrame::kMaxCommandBytes)
    {
        return Status::InvalidFrame;
    }
    // 先做减法再比较，避免加法溢出
    if (payloadSize >
        ProtocolFrame::kMaxFrameBytes - ProtocolFrame::kHeaderBytes - commandSize)
    {
        return Status::InvalidFrame;
    }

    const std::size_t frameSize =
        ProtocolFrame::kHeaderBytes + commandSize + payloadSize;

    // 先写到临时对象，成功后再换给调用方，失败时 out 保持原样
    std::string encoded;
    encoded.reserve(frameSize);
    writeBigEndian(encoded, frameSize, ProtocolFrame::kPacketLengthBytes);
    writeBigEndian(encoded, commandSize, ProtocolFrame::kCommandLengthBytes);
    encoded.append(frame.command);
    encoded.append(frame.payload);

    out.swap(encoded);
    return Status::Ok;
}

FrameCodec::Status FrameCodec::decode(const char *data, std::size_t size, CommandFrame &frame)
{
    if (data == nullptr)
    {
        return Status::InvalidFrame;
    }

    std::size_t frameSize = 0;
    CommandFrame parsed;
    const Status status = parseFrame(data, size, frameSize, parsed);
    if (status != Status::Ok)
    {
        return status;
    }
    if (size != frameSize)
    {
        return Status::InvalidFrame; // 尾巴上还有多余字节，说明不是「正好一帧」
    }

    frame = std::move(parsed);
    return Status::Ok;
}

FrameCodec::Status FrameCodec::decode(const std::string &data, CommandFrame &frame)
{
    return decode(data.data(), data.size(), frame);
}

void FrameCodec::push(const char *data, std::size_t size)
{
    if (data == nullptr || size == 0)
    {
        return;
    }

    // 上一批数据已经全部取走时直接复用缓冲，否则继续往后面追加
    if (_consumed > 0 && _consumed == _buffer.size())
    {
        _buffer.clear();
        _consumed = 0;
    }
    _buffer.append(data, size);
}

void FrameCodec::push(const std::string &data)
{
    push(data.data(), data.size());
}

FrameCodec::Status FrameCodec::next(CommandFrame &frame)
{
    const char *base = _buffer.data() + _consumed;
    const std::size_t available = _buffer.size() - _consumed;

    std::size_t frameSize = 0;
    const Status status = parseFrame(base, available, frameSize, frame);

    switch (status)
    {
    case Status::Ok:
        _consumed += frameSize;
        if (_consumed == _buffer.size())
        {
            _buffer.clear();
            _consumed = 0;
        }
        else if (_consumed >= kCompactThreshold)
        {
            _buffer.erase(0, _consumed);
            _consumed = 0;
        }
        return Status::Ok;

    case Status::InvalidFrame:
        // 帧头就不合法，在这种字节流里没法可靠地找回边界，丢弃缓冲重新开始
        reset();
        return Status::InvalidFrame;

    case Status::NeedMoreData:
    default:
        return Status::NeedMoreData;
    }
}

std::size_t FrameCodec::pendingBytes() const
{
    return _buffer.size() - _consumed;
}

void FrameCodec::reset()
{
    _buffer.clear();
    _consumed = 0;
}

const char *FrameCodec::statusName(Status status)
{
    switch (status)
    {
    case Status::Ok:
        return "Ok";
    case Status::NeedMoreData:
        return "NeedMoreData";
    case Status::InvalidFrame:
        return "InvalidFrame";
    }
    return "Unknown";
}
