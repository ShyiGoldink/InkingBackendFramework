#!/usr/bin/env python3
"""保持 N 条连接，可选让每条各发一次并校验回显。

【适用范围】本脚本发的是**裸字节**，只适用于 A6 时代的 echo 服务端。
C1 之后服务端只认带帧协议（魔数不对直接断），请改用 framed_client.py。

用法：
    python3 tools/netbench/hold_conns.py [条数] [保持秒数] [--echo] [--timeout 秒] [--host H] [--port P]

例子：
    python3 tools/netbench/hold_conns.py 1000 60                  # 1000 条连上并保持 60 秒
    python3 tools/netbench/hold_conns.py 15 0 --echo --timeout 1   # 15 条各发一次 → 旧的 A3 只能过 9 条
    python3 tools/netbench/hold_conns.py 1000 0 --echo --timeout 1 # 1000 条各发一次

只有「全部成功」才返回 0，任何一条失败/超时都返回 1，方便脚本判定。

两处平台坑（这也是它为什么不是十行脚本）：
1. Windows 的 select() 用的是编译期固定大小的 fd_set，FD_SETSIZE 默认 512，
   一次塞 1000 个 fd 会直接报 "too many file descriptors in select()"。
   所以这里把所有 fd 分成每批 <=400，批次之间复用少量工作线程。
2. 服务端是一条线程管 1000 条，客户端这边如果一条连接开一条线程，1000 条线程
   会先把客户端自己拖垮，于是根本量不出服务端的问题。所以这里全程非阻塞 +
   事件等待——顺带也是一份「单线程管 N 条」的参照物：它在做的事，
   正是你要在 A6 的服务端做的事。
"""

import argparse
import select
import socket
import sys
import threading
import time

# Windows 控制台默认不是 UTF-8，中文输出会变成乱码（Linux 上无副作用）
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

ECHO_PAYLOAD = b"hello"
BATCH_SIZE = 400  # 必须 < 512（Windows FD_SETSIZE），留出余量


def parseArgs() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="保持 N 条连接（可顺带验证 echo）")
    parser.add_argument("count", type=int, help="连接条数")
    parser.add_argument("hold", type=float, nargs="?", default=0.0, help="连上后保持多少秒")
    parser.add_argument("--echo", action="store_true", help="每条连接各发一次 hello 并校验回显")
    parser.add_argument("--timeout", type=float, default=5.0, help="--echo 时每条的等待上限（秒）")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9000)
    parser.add_argument("--verbose", action="store_true", help="逐条打印结果")
    return parser.parse_args()


def batches(items: list, size: int = BATCH_SIZE) -> list:
    return [items[i:i + size] for i in range(0, len(items), size)]


def connectAll(host: str, port: int, count: int, verbose: bool) -> list:
    """分批非阻塞 connect，返回连上的 socket 列表。

    每批都先把 connect 全部发起（非阻塞，立刻返回），再用 select 等这批可写。
    这样即使 1000 条一起连，也不会阻塞在任何一条上。
    """
    connected = []
    begin = time.monotonic()

    for groupIndex, groupSize in enumerate([BATCH_SIZE] * (count // BATCH_SIZE) + [count % BATCH_SIZE]):
        if groupSize == 0:
            continue
        pending = {}
        for _ in range(groupSize):
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.setblocking(False)
            try:
                sock.connect((host, port))
            except BlockingIOError:
                pass  # 正常：连接进行中，等可写
            except OSError as error:
                if verbose:
                    print(f"  连接请求直接失败: {error}")
                sock.close()
                continue
            pending[sock.fileno()] = sock

        # 等这批 connect 完成：可写 = 连上了（也可能是失败，用 SO_ERROR 判）
        deadline = time.monotonic() + 30.0
        while pending and time.monotonic() < deadline:
            _, writable, exceptional = select.select([], list(pending.values()), list(pending.values()), 0.5)
            for sock in writable + exceptional:
                fd = sock.fileno()
                errorCode = sock.getsockopt(socket.SOL_SOCKET, socket.SO_ERROR)
                if errorCode == 0:
                    connected.append(sock)
                else:
                    if verbose:
                        print(f"  fd {fd} 连接失败: SO_ERROR={errorCode}")
                    sock.close()
                pending.pop(fd, None)
        for sock in pending.values():
            if verbose:
                print("  连接超时（30 秒内没连上）")
            sock.close()

        if verbose:
            print(f"  第 {groupIndex + 1} 批完成，累计连上 {len(connected)}/{count}")

    elapsed = time.monotonic() - begin
    rate = count / elapsed if elapsed > 0 else 0.0
    print(f"连接建立: {len(connected)}/{count} 条，耗时 {elapsed:.2f} 秒（{rate:.0f} 条/秒）")
    return connected


def echoBatch(socks: list, timeout: float, verbose: bool) -> int:
    """一批连接各发一次 hello 并校验回显，返回成功的条数。

    读写都非阻塞：send 可能只发一半（剩下的游标留着等可写），
    recv 可能只收到半个消息（累计到够长才算这条完成）。
    """
    payloadSize = len(ECHO_PAYLOAD)
    pendingSend = {sock: 0 for sock in socks}
    received = {sock: b"" for sock in socks}
    failed = set()
    done = set()

    for sock in socks:
        sock.setblocking(False)

    # 先尽量把请求发出去
    for sock in socks:
        try:
            pendingSend[sock] += sock.send(ECHO_PAYLOAD)
        except BlockingIOError:
            pass
        except OSError:
            failed.add(sock)
            done.add(sock)

    deadline = time.monotonic() + max(timeout, 1.0)
    while len(done) < len(socks) and time.monotonic() < deadline:
        alive = [s for s in socks if s not in done]
        if not alive:
            break
        readable, writable, _ = select.select(alive, alive, [], 0.05)

        for sock in writable:
            if sock in done or pendingSend.get(sock, 0) >= payloadSize:
                continue
            try:
                pendingSend[sock] += sock.send(ECHO_PAYLOAD[pendingSend[sock]:])
            except BlockingIOError:
                pass
            except OSError:
                failed.add(sock)
                done.add(sock)

        for sock in readable:
            if sock in done:
                continue
            try:
                chunk = sock.recv(4096)
            except BlockingIOError:
                continue
            except OSError:
                failed.add(sock)
                done.add(sock)
                continue
            if not chunk:
                failed.add(sock)  # 还没收够就被关了
                done.add(sock)
                continue
            received[sock] += chunk
            if len(received[sock]) >= payloadSize:
                done.add(sock)

    success = 0
    for sock in socks:
        if sock in failed:
            if verbose:
                print(f"  fd {sock.fileno()} 失败")
            continue
        if sock not in done:
            failed.add(sock)  # 等到超时还没收全
            if verbose:
                print(f"  fd {sock.fileno()} 超时，只收到 {len(received[sock])}/{payloadSize} 字节")
            continue
        if received[sock][:payloadSize] != ECHO_PAYLOAD:
            if verbose:
                print(f"  fd {sock.fileno()} 回显不对: {received[sock][:payloadSize]!r}")
            continue
        success += 1

    return success


def holdBatch(socks: list, stopEvent: threading.Event, verbose: bool) -> int:
    """把这一批保持到 stopEvent 被设置，返回期间被服务端断开的条数。"""
    dropped = 0
    alive = list(socks)
    while alive and not stopEvent.is_set():
        readable, _, exceptional = select.select(alive, [], alive, 0.2)
        for sock in readable + exceptional:
            try:
                chunk = sock.recv(1)
            except BlockingIOError:
                continue
            except OSError:
                chunk = b""
            if not chunk:
                dropped += 1
                alive.remove(sock)
                if verbose:
                    print(f"  保持期间 fd {sock.fileno()} 被断开")
                sock.close()
    return dropped


def main() -> int:
    args = parseArgs()
    if args.count <= 0:
        print("条数必须大于 0")
        return 2

    print(f"目标 {args.host}:{args.port}，条数 {args.count}，保持 {args.hold} 秒，echo={args.echo}")
    socks = connectAll(args.host, args.port, args.count, args.verbose)
    if not socks:
        print("一条都没连上（服务端跑起来了吗？控制台里执行 network-run）")
        return 1

    result = {"echoSuccess": None, "dropped": 0}
    lock = threading.Lock()
    groups = batches(socks)
    stopEvent = threading.Event()

    def worker(group: list) -> None:
        batchEcho = None
        if args.echo:
            batchEcho = echoBatch(group, args.timeout, args.verbose)
        holdDropped = holdBatch(group, stopEvent, args.verbose)
        with lock:
            if batchEcho is not None:
                result["echoSuccess"] = (result["echoSuccess"] or 0) + batchEcho
            result["dropped"] += holdDropped

    workers = [threading.Thread(target=worker, args=(group,)) for group in groups]
    for thread in workers:
        thread.start()

    if args.hold > 0:
        time.sleep(args.hold)
    stopEvent.set()
    for thread in workers:
        thread.join()

    for sock in socks:
        sock.close()

    connectedCount = len(socks)
    if args.echo:
        print(f"echo: {result['echoSuccess'] or 0}/{connectedCount} 条成功")
    if args.hold > 0:
        print(f"保持 {args.hold:.0f} 秒结束：期间被断开 {result['dropped']} 条")

    if connectedCount != args.count:
        print(f"结论：失败 —— 只连上 {connectedCount}/{args.count} 条")
        return 1
    if args.echo and (result["echoSuccess"] or 0) != args.count:
        print(f"结论：失败 —— echo 只有 {result['echoSuccess'] or 0}/{args.count} 条成功")
        return 1
    if result["dropped"] > 0:
        print(f"结论：失败 —— 保持期间有 {result['dropped']} 条被服务端断开")
        return 1
    print("结论：成功")
    return 0


if __name__ == "__main__":
    sys.exit(main())
