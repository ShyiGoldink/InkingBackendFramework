#!/usr/bin/env python3
"""C6 验收：停机时「在飞请求不丢、也不出现半个帧」。

用法（服务端用固定延时触发退出，例如 `{ sleep 12; echo exit; } | ./exe`）：

    python tools/netbench/shutdown_client.py --case inflight --conns 100 --duration 8
    python tools/netbench/shutdown_client.py --case idle --conns 200 --duration 8

三个用例：
    inflight  每条连接「发一条、等一条」地循环。停机发生时，要么这一条请求拿到
              完整响应，要么连接在帧边界处被断开——**不允许读到半个帧**。
    idle      只连不发。停机时应当收到干净的 FIN（不是 RST）。
    slow      只发不读，让服务端的写缓冲排不空，用于验证「排空上限」到点会放弃，
              而不是让进程永远停不下来（这条用例看的是服务端日志与退出耗时）。

判读方式（为什么是「一条在飞」而不是「猛发」）：
客户端如果不停地灌请求，服务端停机时接收缓冲里必然压着一堆没读的数据，
`close()` 会因为「有未读数据」发出 RST，而 RST 可能让客户端丢掉已经到达的响应。
那种情况下「响应丢了」不是停机的错——那些请求服务端从来没受理过。
改成发一条等一条之后，任意时刻只有两种状态：要么有请求在飞（排空会等它），
要么刚发出一条还没被读（断开在帧边界，客户端一个字节都没收到），两者都不该出现半个帧。
"""

import argparse
import select
import socket
import sys
import threading
import time

from framed_client import HEADER_BYTES, MAGIC, make_frame, recv_exact

for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

PAYLOAD = b"s" * 256


class Result:
    def __init__(self):
        self.lock = threading.Lock()
        self.completed = 0        # 收到完整响应的请求数
        self.after_shutdown = 0   # 其中「第一次观察到断开之后」收到的
        self.clean_close = 0      # 帧边界处的干净断开
        self.reset = 0            # 被 RST
        self.truncated = 0        # 半个帧（失败）
        self.connected = 0        # 成功建立的连接数
        self.errors = []
        self.shutdown_seen = False

    def note(self, key, value=1):
        with self.lock:
            setattr(self, key, getattr(self, key) + value)

    def note_close(self, kind, detail=""):
        with self.lock:
            if not self.shutdown_seen:
                self.shutdown_seen = True
            setattr(self, kind, getattr(self, kind) + 1)
            if detail and len(self.errors) < 5:
                self.errors.append(detail)

    def response_done(self):
        with self.lock:
            self.completed += 1
            if self.shutdown_seen:
                self.after_shutdown += 1


def connect(host, port, timeout):
    sock = socket.create_connection((host, port), timeout=timeout)
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    return sock


def read_one_frame(sock):
    """读一整帧；返回 (状态, 已读字节数)

    状态：ok / eof / reset / truncated
    """
    read_bytes = 0
    try:
        header = recv_exact_tracked(sock, HEADER_BYTES)
        if header is None:
            return "eof", read_bytes
        read_bytes += len(header)
        if len(header) < HEADER_BYTES:
            return "truncated", read_bytes

        magic = int.from_bytes(header[:2], "big")
        total = int.from_bytes(header[2:6], "big")
        if magic != MAGIC or total < HEADER_BYTES:
            return "truncated", read_bytes

        body = recv_exact_tracked(sock, total - HEADER_BYTES)
        if body is None:
            return "truncated", read_bytes  # 帧头收到了、帧体没读全 = 半个帧
        read_bytes += len(body)
        return "ok", read_bytes
    except (ConnectionResetError, ConnectionAbortedError):
        return "reset", read_bytes
    except socket.timeout:
        return "timeout", read_bytes


def recv_exact_tracked(sock, size):
    """读满 size 字节；对端在读到任何字节之前关闭返回 None"""
    chunks = []
    remaining = size
    while remaining > 0:
        try:
            chunk = sock.recv(remaining)
        except (ConnectionResetError, ConnectionAbortedError):
            raise
        if not chunk:
            return None if not chunks else b"".join(chunks)
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def worker(host, port, duration, result, kind):
    deadline = time.monotonic() + duration
    frame = make_frame("echo", PAYLOAD)

    try:
        sock = connect(host, port, 5.0)
    except OSError as error:
        result.note_close("reset", f"建连失败: {error}")
        return
    result.note("connected")

    try:
        sock.settimeout(2.0)
        while time.monotonic() < deadline:
            if kind == "idle":
                # 空闲连接的唯一任务就是「活着」，所以这里只做一件事：
                # 等对端把 FIN 送过来。等到了就说明停机把这条连接干净收掉了。
                readable, _, _ = select.select([sock], [], [], 0.05)
                if readable:
                    try:
                        if sock.recv(1) == b"":
                            result.note_close("clean_close")
                            return
                    except (ConnectionResetError, ConnectionAbortedError):
                        result.note_close("reset")
                        return
                    except BlockingIOError:
                        pass
                continue

            # 发下一条之前先看对端有没有已经关了（只看一个字节、不移出缓冲）。
            # 不等这一步直接发的话，服务端已经 FIN 的瞬间我们再发一帧会招来 RST，
            # 统计里就会把「服务端干得漂亮」记成「被重置」——测的是自己不会读。
            try:
                if select.select([sock], [], [], 0)[0] and sock.recv(1, socket.MSG_PEEK) == b"":
                    result.note_close("clean_close")
                    return
            except (ConnectionResetError, ConnectionAbortedError):
                result.note_close("reset")
                return
            except BlockingIOError:
                pass

            try:
                sock.sendall(frame)
            except (ConnectionResetError, ConnectionAbortedError):
                result.note_close("reset")
                return
            except OSError:
                result.note_close("clean_close")
                return

            status, _ = read_one_frame(sock)
            if status == "ok":
                result.response_done()
                continue
            if status == "truncated":
                result.note("truncated")
                return
            if status == "reset":
                result.note_close("reset")
                return
            if status == "eof":
                # 这里一定是帧边界：read_one_frame 在读到帧头之前就返回 eof
                result.note_close("clean_close")
                return
            # timeout：说明服务端没回也没关，继续下一轮
    finally:
        try:
            sock.close()
        except OSError:
            pass


def run_slow(host, port, conns, duration):
    """只发不读：把服务端的写缓冲撑满，验证「排空上限」到点会放弃而不是永远等。

    这条用例的服务端证据在日志里（`排空超时：仍有 N 条连接…`）和退出耗时上，
    客户端这边只负责把连接占住并确认它们最终被断开。
    """
    socks = []
    payload_size = 4 << 20
    frame = make_frame("echo", b"z" * payload_size)
    for _ in range(conns):
        try:
            sock = connect(host, port, 5.0)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 8192)
            sock.sendall(frame)
            socks.append(sock)
        except OSError:
            break

    print(f"  {len(socks)} 条连接各发了 {payload_size / 1048576:.0f} MiB 请求，全程不读")
    time.sleep(duration)

    closed = 0
    for sock in socks:
        sock.settimeout(1.0)
        try:
            # 不能只 recv 一次：这里有几 MiB 响应堆在内核里，第一次 recv 拿到的是数据。
            # 要一直读到 EOF（或读不动）才能判断连接到底断没断。
            while True:
                chunk = sock.recv(262144)
                if not chunk:
                    closed += 1
                    break
        except (socket.timeout, TimeoutError):
            pass
        except OSError:
            closed += 1
        finally:
            try:
                sock.close()
            except OSError:
                pass

    # 不在这里判失败：连接是不是被断开，取决于停机有没有真的发生
    print(f"  停机窗口结束时 {closed}/{len(socks)} 条已被断开")
    print("  [提示] 这条用例要看服务端日志里的「开始排空 / 排空超时」与进程退出耗时")
    return True


def run(host, port, conns, duration, kind):
    result = Result()
    threads = [
        threading.Thread(target=worker, args=(host, port, duration, result, kind), daemon=True)
        for _ in range(conns)
    ]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join(timeout=duration + 15)

    alive = sum(1 for thread in threads if thread.is_alive())
    print(f"  连接 {conns} 条，{kind} 模式，{duration:.0f} 秒窗口")
    if kind != "idle":
        print(f"  完整响应 {result.completed} 条（其中停机开始后仍拿到 {result.after_shutdown} 条）")
    print(f"  帧边界干净断开 {result.clean_close} 条，被 RST {result.reset} 条，"
          f"半个帧 {result.truncated} 条，未结束 {alive} 条")
    for detail in result.errors:
        print(f"    异常: {detail}")

    if result.connected == 0:
        print("  [失败] 一条连接都没连上：确认服务端在跑、端口没被占用")
        return False

    ok = result.truncated == 0 and alive == 0
    if kind == "idle":
        # 空闲连接应当全部被干净断开（服务端主动关，正常情况下不会有 RST）
        ok = ok and result.reset == 0
    print(f"  {'[通过]' if ok else '[失败]'} 停机不产生半个帧")
    return ok


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9000)
    parser.add_argument("--case", default="inflight", choices=["inflight", "idle", "slow"])
    parser.add_argument("--conns", type=int, default=100)
    parser.add_argument("--duration", type=float, default=8.0, help="跑多久（要覆盖到停机时刻）")
    args = parser.parse_args()

    kind = "idle" if args.case == "idle" else "inflight"
    if args.case == "slow":
        return 0 if run_slow(args.host, args.port, args.conns, args.duration) else 1
    return 0 if run(args.host, args.port, args.conns, args.duration, kind) else 1


if __name__ == "__main__":
    sys.exit(main())
