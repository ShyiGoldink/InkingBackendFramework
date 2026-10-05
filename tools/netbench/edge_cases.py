#!/usr/bin/env python3
"""A6 验收第 4 条：RST 异常断开 / 半包 / 全部关闭后不泄漏。

用法：
    python3 tools/netbench/edge_cases.py [--host H] [--port P]

做四件事，任何一步不符合预期就以非 0 退出：
  1. 建立 20 条连接，其中若干条用 SO_LINGER=0 强制 RST 断开；
     验证剩下的连接仍然能正常 echo（异常断开不能影响别人）。
  2. 半包：把一条消息拆成两段、中间停一下再发第二段，
     验证服务端把两段当成一条消息处理（收到多少字节就回多少字节）。
  3. 连发粘包：一次 write 把三条消息连在一起发，验证收回来的是同样长度的字节。
  4. 全部关闭后，再连一条新连接做一次 echo——证明前面那些断开没有把服务拖坏。

第 1 条用的是 SO_LINGER=0：close 时不走正常的四次挥手，直接发 RST。
它和「正常关闭」在服务端是两条不同的路径（recv 返回 0 vs recv 返回 -1/ECONNRESET），
这两条路都必须把连接从事件集合和连接表里删掉，否则就是那句「客户端断开后进程 CPU 升高」。
"""

import argparse
import socket
import struct
import sys
import time

for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

RESULTS = []


def check(name: str, passed: bool, detail: str = "") -> None:
    RESULTS.append((name, passed, detail))
    print(f"[{'PASS' if passed else 'FAIL'}] {name}{(' —— ' + detail) if detail else ''}")


def connect(host: str, port: int, timeout: float = 5.0) -> socket.socket:
    sock = socket.create_connection((host, port), timeout=timeout)
    sock.settimeout(timeout)
    return sock


def echo(sock: socket.socket, payload: bytes) -> bytes:
    sock.sendall(payload)
    got = b""
    while len(got) < len(payload):
        chunk = sock.recv(len(payload) - len(got))
        if not chunk:
            break
        got += chunk
    return got


def rstClose(sock: socket.socket) -> None:
    """SO_LINGER=0 + close：直接发 RST，模拟客户端被强杀"""
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
    sock.close()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9000)
    args = parser.parse_args()

    # --- 1. RST 异常断开不影响其它连接 ---
    total = 20
    victims = {2, 5, 9, 14}
    socks = []
    try:
        for index in range(total):
            socks.append(connect(args.host, args.port))
    except OSError as error:
        print(f"连接失败: {error}（服务端跑起来了吗？）")
        return 2

    for index in victims:
        rstClose(socks[index])
    time.sleep(0.5)

    survivorsOk = 0
    for index, sock in enumerate(socks):
        if index in victims:
            continue
        try:
            if echo(sock, b"after-rst") == b"after-rst":
                survivorsOk += 1
        except OSError as error:
            print(f"  幸存连接 {index} 出错: {error}")
    check(f"{total - len(victims)} 条幸存连接在 4 条 RST 之后仍能 echo",
          survivorsOk == total - len(victims),
          f"{survivorsOk}/{total - len(victims)}")

    # --- 2. 半包：拆两段发 ---
    probe = connect(args.host, args.port)
    probe.sendall(b"hel")
    time.sleep(0.3)  # 故意让服务端先看到半条消息
    probe.sendall(b"lo")
    got = b""
    deadline = time.time() + 5
    while len(got) < 5 and time.time() < deadline:
        chunk = probe.recv(64)
        if not chunk:
            break
        got += chunk
    check("半包：拆成 hel + lo 两段，回显 hello", got == b"hello", f"收到 {got!r}")

    # --- 3. 粘包：一次发三条 ---
    probe.sendall(b"aaabbbccc")
    got = b""
    deadline = time.time() + 5
    while len(got) < 9 and time.time() < deadline:
        chunk = probe.recv(64)
        if not chunk:
            break
        got += chunk
    check("粘包：一次发 9 字节，原样收回 9 字节", got == b"aaabbbccc", f"收到 {got!r}")

    # --- 4. 大消息：一次 5000 字节 ---
    big = b"z" * 5000
    got = echo(probe, big)
    check("5000 字节大消息完整回显", got == big, f"长度 {len(got)}")

    for sock in socks:
        if sock.fileno() != -1:
            try:
                sock.close()
            except OSError:
                pass
    probe.close()
    time.sleep(0.5)

    # --- 5. 全部断开后再来一条新连接 ---
    try:
        fresh = connect(args.host, args.port)
        ok = echo(fresh, b"still-alive") == b"still-alive"
        fresh.close()
        check("全部断开后服务仍能接受新连接并 echo", ok)
    except OSError as error:
        check("全部断开后服务仍能接受新连接并 echo", False, str(error))

    failed = [name for name, passed, _ in RESULTS if not passed]
    print("-" * 40)
    if failed:
        print(f"结论：失败 —— {len(failed)} 项未过：{'; '.join(failed)}")
        return 1
    print("结论：成功（RST / 半包 / 粘包 / 大消息 / 复用 全部通过）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
