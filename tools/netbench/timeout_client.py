#!/usr/bin/env python3
"""C5 验收：三类超时（半包 / 空闲 / 写）是否真的把连接回收掉。

用法（服务端的 networkConfig 里要把三个超时调短，否则等十分钟）：

    # 第 1 轮：关掉空闲超时，测半包与写
    #   idleTimeoutMs = 0, halfFrameTimeoutMs = 2000, writeTimeoutMs = 3000
    python tools/netbench/timeout_client.py --case half,write \\
        --half-timeout 2 --write-timeout 3

    # 第 2 轮：打开空闲超时，测空闲
    #   idleTimeoutMs = 3000
    python tools/netbench/timeout_client.py --case idle --idle-timeout 3

为什么要分两轮：写超时用例必须「只发不读」，而响应一旦被内核全部吞下，服务端就会
写回 Idle 并重新挂上空闲超时——两轮的时长一样时，先到期的永远是空闲超时，
测出来的归因是错的（本文档的上一版就踩了这个坑）。关掉空闲超时之后，
能关掉那条连接的只剩写超时，收到 FIN 就一定是它。

用例：
    half   发一个帧头声明 200 字节、实际只发 40 字节 → 半包超时应该断开
    idle   连上什么也不发 → 空闲超时应该断开（服务端 idleTimeoutMs=0 时跳过）
    write  发一条大请求但完全不读响应 → 写超时应该断开

判读方式：每个用例记录「从动作完成到收到 FIN」的耗时，落在
[0.8×超时, 超时+3 秒] 内算通过。区间下限的意义是「不能提前杀」，
上限的意义是「不能拖」。只看「最终断了」是不够的——把超时写成 0
（等于关闭）时连接也会因为别的原因断，那种通过是假通过。
归因（到底是哪一类超时）以服务端日志的分片摘要为准：
每个分片退出时会打印「半包超时 / 空闲超时 / 写超时」各几条。
"""

import argparse
import socket
import sys
import time

from framed_client import make_frame

for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass


def connect(host: str, port: int, timeout: float, receive_buffer: int = 0) -> socket.socket:
    sock = socket.create_connection((host, port), timeout=timeout)
    if receive_buffer > 0:
        # 把客户端的接收窗口压小，服务端才会很快「写不动」。
        # 不压的话内核会替它收下几百 KB，写超时要等更久才触发。
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, receive_buffer)
    return sock


def wait_for_close(sock: socket.socket, budget: float):
    """等对端关闭，返回 (是否关闭, 耗时秒)。预算用尽返回 (False, budget)"""
    start = time.monotonic()
    sock.settimeout(budget)
    try:
        while True:
            chunk = sock.recv(65536)
            if not chunk:
                return True, time.monotonic() - start
            # 收到数据说明还没关（正常不该发生：这三个用例都不该有回包）
    except socket.timeout:
        return False, time.monotonic() - start
    except (ConnectionResetError, ConnectionAbortedError):
        return True, time.monotonic() - start


def judge(name: str, closed: bool, elapsed: float, expected: float) -> bool:
    low = expected * 0.8
    high = expected + 3.0
    if not closed:
        print(f"  [失败] {name}：{expected:.1f} 秒超时窗口内没有断开")
        return False
    if elapsed < low:
        print(f"  [失败] {name}：{elapsed:.2f} 秒就被断开，比预期 {expected:.1f} 秒早太多")
        return False
    if elapsed > high:
        print(f"  [失败] {name}：{elapsed:.2f} 秒才断开，超出预期 {expected:.1f} 秒 + 3 秒")
        return False
    print(f"  [通过] {name}：{elapsed:.2f} 秒被断开（预期 {expected:.1f} 秒）")
    return True


def case_half(host: str, port: int, timeout: float, expected: float) -> bool:
    """发一个「说 200 字节、只给 40 字节」的残帧，然后等"""
    sock = connect(host, port, timeout)
    try:
        frame = make_frame("echo", b"x" * 192)  # 8 字节帧头 + 4 字节指令名 + 192
        sock.sendall(frame[:40])
        closed, elapsed = wait_for_close(sock, expected + 3.0)
        return judge("半包超时", closed, elapsed, expected)
    finally:
        sock.close()


def case_idle(host: str, port: int, timeout: float, expected: float) -> bool:
    """连上什么也不发"""
    sock = connect(host, port, timeout)
    try:
        closed, elapsed = wait_for_close(sock, expected + 3.0)
        return judge("空闲超时", closed, elapsed, expected)
    finally:
        sock.close()


def case_write(host: str, port: int, timeout: float, expected: float, payload: int) -> bool:
    """发一条大请求、完全不读响应：服务端写缓冲填满后会挂住，直到写超时"""
    sock = connect(host, port, timeout, receive_buffer=8192)
    try:
        sock.sendall(make_frame("echo", b"y" * payload))
        time.sleep(expected + 1.5)

        # 这段之后才开始读：先把服务端能塞过来的数据读干净，再看有没有 FIN。
        # 服务端若是正常发完（没超时），连接会回到空闲并保持打开，读到最后是超时而不是空包。
        sock.settimeout(5.0)
        drained = 0
        closed = False
        try:
            while True:
                chunk = sock.recv(262144)
                if not chunk:
                    closed = True
                    break
                drained += len(chunk)
        except socket.timeout:
            closed = False
        except (ConnectionResetError, ConnectionAbortedError):
            closed = True

        if not closed:
            print(f"  [失败] 写超时：读完 {drained} 字节后连接仍然活着（服务端没判定写超时）")
            return False
        if drained == 0:
            print("  [失败] 写超时：一个字节都没收到，说明服务端在写之前就断了（不是写超时）")
            return False
        # `drained` 只能说明「收了多少」，不能用来判断有没有被写阻塞：
        # 服务端 close 之后内核仍会把已排队的字节发完，读到全量也正常。
        # 所以本用例要求服务端**关掉空闲超时**（idleTimeoutMs=0）：此时能关这条连接的
        # 只有写超时，收到 FIN 就是写超时生效。
        print(f"  [通过] 写超时：请求 {payload / 1048576:.1f} MiB、全程不读，"
              f"服务端在 {expected:.1f} 秒量级关闭（共收到 {drained / 1048576:.2f} MiB）")
        return True
    finally:
        sock.close()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9000)
    parser.add_argument("--timeout", type=float, default=10.0, help="建连超时")
    parser.add_argument("--case", default="all", help="all / half / idle / write，可用逗号组合")
    parser.add_argument("--half-timeout", type=float, default=2.0, help="服务端 halfFrameTimeoutMs，单位秒")
    parser.add_argument("--idle-timeout", type=float, default=3.0, help="服务端 idleTimeoutMs，单位秒；0 表示关闭")
    parser.add_argument("--write-timeout", type=float, default=3.0, help="服务端 writeTimeoutMs，单位秒")
    parser.add_argument("--write-payload", type=int, default=8 << 20, help="写超时用例的请求负载（字节）")
    args = parser.parse_args()

    # 允许 --case half,write 这样的组合：写超时要求空闲超时关闭，
    # 两轮服务端配置不同，脚本要能分开跑。
    selected = {part.strip() for part in args.case.split(",") if part.strip()}

    results = []
    if selected & {"all", "half"}:
        results.append(("半包超时", case_half(args.host, args.port, args.timeout, args.half_timeout)))
    if selected & {"all", "idle"}:
        if args.idle_timeout <= 0:
            print("  [跳过] 空闲超时：服务端 idleTimeoutMs=0（关闭）")
        else:
            results.append(("空闲超时", case_idle(args.host, args.port, args.timeout, args.idle_timeout)))
    if selected & {"all", "write"}:
        results.append(
            ("写超时", case_write(args.host, args.port, args.timeout, args.write_timeout, args.write_payload))
        )

    passed = sum(1 for _, ok in results if ok)
    print(f"\n合计 {passed}/{len(results)} 通过")
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
