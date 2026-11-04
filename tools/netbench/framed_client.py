#!/usr/bin/env python3
"""C1 验收：按帧协议打服务端（半包 / 粘包 / 非法帧 / 1000 连接）。

用法：
    python tools/netbench/framed_client.py --case all
    python tools/netbench/framed_client.py --case echo --conns 1000 --hold 5
    python tools/netbench/framed_client.py --case half
    python tools/netbench/framed_client.py --host 127.0.0.1 --port 9000

用例（对应设计文档 M6 的六类）：
    echo     N 条连接各发一帧，校验回显与请求逐字节相同
    half     一帧拆成三段、中间停一下再发 → 验证半包
    sticky   三帧一次 write 发出 → 验证粘包
    big      5000 字节负载 → 验证大包不被截断
    badmagic 魔数不对 → 服务端应断开
    badlen   包长字段小于帧头 → 服务端应断开
    badcmd   指令长超过上限 → 服务端应断开
    hold     N 条连接只连不发，保持 S 秒不断（配合 --conns/--hold）

为什么另起一个脚本而不是改 hold_conns.py：
C1 之后服务端只认带帧的数据，裸字节会被魔数判定挡住（端口扫描、错版本客户端
都必须在最短路径上被挡掉）。hold_conns.py / edge_cases.py 是 A6 时代裸字节 echo
的验收脚本，它们对现在的服务端已经不再适用，保留它们是为了能回溯当时的结论。

Windows 上 select 的 fd_set 是编译期固定大小（FD_SETSIZE=512），所以 echo 用例
按每批 400 条连接处理，和 hold_conns.py 的做法一致。
"""

import argparse
import socket
import struct
import sys
import time

MAGIC = 0xABCD
HEADER_BYTES = 8
MAX_COMMAND_BYTES = 1024
BATCH_SIZE = 400

for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass


def make_frame(command: str, payload: bytes = b"") -> bytes:
    """按 ProtocolFrame 的布局组帧：魔数(2) + 包长(4) + 指令长(2) + 指令 + 数据"""
    cmd = command.encode("utf-8")
    total = HEADER_BYTES + len(cmd) + len(payload)
    header = MAGIC.to_bytes(2, "big") + total.to_bytes(4, "big") + len(cmd).to_bytes(2, "big")
    return header + cmd + payload


def make_header(magic: int, total: int, command_length: int) -> bytes:
    """故意组一个非法帧头，用来触发 InvalidFrame"""
    return struct.pack(">HIH", magic, total, command_length)


def recv_exact(sock: socket.socket, size: int):
    """读满 size 字节；对端关闭返回 None"""
    chunks = []
    remaining = size
    while remaining > 0:
        try:
            chunk = sock.recv(remaining)
        except (ConnectionResetError, ConnectionAbortedError):
            return None
        if not chunk:
            return None
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def read_frame(sock: socket.socket):
    """读回一整帧的原始字节（不做解释）；对端关闭返回 None"""
    header = recv_exact(sock, HEADER_BYTES)
    if header is None:
        return None
    magic, total, command_length = struct.unpack(">HIH", header)
    if magic != MAGIC:
        raise ValueError(f"回包的魔数不对：0x{magic:04X}")
    if total < HEADER_BYTES:
        raise ValueError(f"回包的包长非法：{total}")
    body = recv_exact(sock, total - HEADER_BYTES)
    if body is None:
        return None
    del command_length
    return header + body


def expect_closed(sock: socket.socket, timeout: float) -> bool:
    """服务端应当主动断开：读到 EOF（或 RST）算通过"""
    sock.settimeout(timeout)
    try:
        data = sock.recv(4096)
    except (ConnectionResetError, ConnectionAbortedError):
        return True
    except socket.timeout:
        return False
    return data == b""


def connect(host: str, port: int, timeout: float) -> socket.socket:
    sock = socket.create_connection((host, port), timeout)
    sock.settimeout(timeout)
    return sock


def case_echo(host: str, port: int, conns: int, timeout: float) -> bool:
    """N 条连接各发一帧，校验回显与请求逐字节相同"""
    request = make_frame("echo", b"hello")
    succeeded = 0
    started = time.time()

    for base in range(0, conns, BATCH_SIZE):
        batch = min(BATCH_SIZE, conns - base)
        sockets = []
        try:
            for _ in range(batch):
                sockets.append(connect(host, port, timeout))
            for sock in sockets:
                sock.sendall(request)
            for sock in sockets:
                echoed = read_frame(sock)
                if echoed == request:
                    succeeded += 1
        except OSError as error:
            print(f"  echo 批次失败（已有 {succeeded} 条成功）：{error}")
            for sock in sockets:
                sock.close()
            return False
        finally:
            for sock in sockets:
                try:
                    sock.close()
                except OSError:
                    pass

    elapsed = time.time() - started
    print(f"  连接建立并回显：{succeeded}/{conns} 条，耗时 {elapsed:.2f} 秒")
    return succeeded == conns


def case_half(host: str, port: int, timeout: float) -> bool:
    """半包：一帧拆成三段，中间停一下"""
    sock = connect(host, port, timeout)
    try:
        frame = make_frame("echo", b"half-packet")
        sock.sendall(frame[:3])
        time.sleep(0.15)
        sock.sendall(frame[3:9])
        time.sleep(0.15)
        sock.sendall(frame[9:])
        echoed = read_frame(sock)
        if echoed != frame:
            print(f"  半包回显不匹配：期望 {len(frame)} 字节，收到 {0 if echoed is None else len(echoed)}")
            return False
        print(f"  半包：三段发送，回显 {len(echoed)} 字节，与请求一致")
        return True
    finally:
        sock.close()


def case_sticky(host: str, port: int, timeout: float) -> bool:
    """粘包：三帧一次 write"""
    sock = connect(host, port, timeout)
    try:
        frames = [make_frame("echo", b"A"), make_frame("echo", b"BBB"), make_frame("echo", b"")]
        blob = b"".join(frames)
        sock.sendall(blob)
        echoed = b""
        for _ in frames:
            one = read_frame(sock)
            if one is None:
                print("  粘包：连接被提前关闭")
                return False
            echoed += one
        if echoed != blob:
            print(f"  粘包回显不匹配：期望 {len(blob)} 字节，收到 {len(echoed)} 字节")
            return False
        print(f"  粘包：一次发 {len(frames)} 帧，逐帧收回 {len(echoed)} 字节，顺序与内容一致")
        return True
    finally:
        sock.close()


def case_big(host: str, port: int, timeout: float) -> bool:
    """大包：5000 字节负载（超过单次 recv 的 4096）"""
    sock = connect(host, port, timeout)
    try:
        payload = bytes((index % 251) for index in range(5000))
        frame = make_frame("echo", payload)
        sock.sendall(frame)
        echoed = read_frame(sock)
        if echoed != frame:
            print("  大包回显不匹配")
            return False
        print(f"  大包：{len(frame)} 字节完整回显（超过单次 recv 的 4096）")
        return True
    finally:
        sock.close()


def case_invalid(host: str, port: int, timeout: float, name: str, header: bytes) -> bool:
    """非法帧头：服务端必须断开，而不是继续在同一字节流上解析"""
    sock = connect(host, port, timeout)
    try:
        sock.sendall(header)
        if not expect_closed(sock, timeout):
            print(f"  {name}：服务端没有断开")
            return False
        print(f"  {name}：服务端按预期断开连接")
        return True
    finally:
        sock.close()


def case_slow(host: str, port: int, timeout: float, frames: int = 400) -> bool:
    """慢消费者：只发不读，服务端写缓冲积压超上限后必须断开

    不断读是关键：一旦开始 recv，服务端的积压就被排空了，永远碰不到上限。
    这里只发、不读，直到对端断开（sendall 抛 RST）为止。
    """
    sock = connect(host, port, timeout)
    frame = make_frame("echo", b"x" * (64 * 1024))
    sent = 0
    closed = False
    try:
        for _ in range(frames):
            sock.sendall(frame)
            sent += 1
    except OSError:
        closed = True
    finally:
        sock.close()

    if not closed:
        print(f"  慢消费者：发了 {sent} 帧（约 {sent * len(frame) / 1048576:.1f} MiB）仍未断开")
        return False
    print(f"  慢消费者：发到第 {sent} 帧（约 {sent * len(frame) / 1048576:.1f} MiB）时被断开")
    return True


def case_hold(host: str, port: int, conns: int, hold: float, timeout: float) -> bool:
    """只连不发：验证分片在空闲连接上不掉线、不泄漏"""
    sockets = []
    try:
        for _ in range(conns):
            sockets.append(connect(host, port, timeout))
        print(f"  已建立 {len(sockets)} 条连接，保持 {hold:.1f} 秒")
        time.sleep(hold)
        alive = 0
        for sock in sockets:
            sock.setblocking(False)
            try:
                data = sock.recv(1)
                if data == b"":
                    continue  # 收到 EOF：服务端把这条连接关了
                alive += 1  # 有意外字节回来，也算在线（不该发生）
            except (BlockingIOError, InterruptedError):
                alive += 1  # 没数据可读，正是空闲连接该有的样子
            except OSError:
                continue  # RST
            finally:
                sock.setblocking(True)
        print(f"  保持结束：{alive}/{len(sockets)} 条仍在线")
        return alive == len(sockets)
    finally:
        for sock in sockets:
            try:
                sock.close()
            except OSError:
                pass


def main() -> int:
    parser = argparse.ArgumentParser(description="C1 帧协议验收客户端")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9000)
    parser.add_argument("--conns", type=int, default=1000)
    parser.add_argument("--hold", type=float, default=5.0)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument(
        "--case",
        default="all",
        choices=["all", "echo", "half", "sticky", "big", "badmagic", "badlen", "badcmd", "slow", "hold"],
    )
    args = parser.parse_args()

    print(f"目标 {args.host}:{args.port}，用例 {args.case}")

    results = []
    if args.case in ("all", "echo"):
        results.append(("1000 条连接按帧回显", case_echo(args.host, args.port, args.conns, args.timeout)))
    if args.case in ("all", "half"):
        results.append(("半包：一帧拆三段", case_half(args.host, args.port, args.timeout)))
    if args.case in ("all", "sticky"):
        results.append(("粘包：三帧一次发", case_sticky(args.host, args.port, args.timeout)))
    if args.case in ("all", "big"):
        results.append(("大包：5000 字节负载", case_big(args.host, args.port, args.timeout)))
    if args.case in ("all", "badmagic"):
        results.append((
            "非法：魔数不对",
            case_invalid(args.host, args.port, args.timeout, "非法：魔数不对",
                         make_header(0x0000, HEADER_BYTES, 0)),
        ))
    if args.case in ("all", "badlen"):
        results.append((
            "非法：包长小于帧头",
            case_invalid(args.host, args.port, args.timeout, "非法：包长小于帧头",
                         make_header(MAGIC, HEADER_BYTES - 1, 0)),
        ))
    if args.case in ("all", "badcmd"):
        results.append((
            "非法：指令长超上限",
            case_invalid(args.host, args.port, args.timeout, "非法：指令长超上限",
                         make_header(MAGIC, HEADER_BYTES + 32, MAX_COMMAND_BYTES + 64)),
        ))
    if args.case in ("all", "slow"):
        results.append(("慢消费者：只发不读", case_slow(args.host, args.port, args.timeout)))
    if args.case == "hold":
        results.append((
            f"{args.conns} 条空连接保持 {args.hold:.0f} 秒",
            case_hold(args.host, args.port, args.conns, args.hold, args.timeout),
        ))

    print("-" * 40)
    failed = [name for name, passed in results if not passed]
    for name, passed in results:
        print(f"[{'PASS' if passed else 'FAIL'}] {name}")

    if failed:
        print(f"结论：失败 —— {len(failed)} 项未过：{', '.join(failed)}")
        return 1
    print(f"结论：成功（{len(results)} 项全过）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
