#!/usr/bin/env bash
# 超时回收的资源验证：连接数（fd 数）是否在超时之后真的回落。
# 用法：在服务端所在的机器上执行，参数为端口（默认 9000）。
set -u

PORT="${1:-9000}"
PID="$(pgrep -f InkingBackendFramework | head -1)"
if [ -z "$PID" ]; then
    echo "没找到服务进程"
    exit 1
fi

count_fds() { ls "/proc/$PID/fd" 2>/dev/null | wc -l; }

echo "服务进程 $PID，空闲基线 fd = $(count_fds)"

python3 - "$PORT" <<'EOF' &
import socket, sys, time
port = int(sys.argv[1])
socks = []
for _ in range(300):
    socks.append(socket.create_connection(("127.0.0.1", port), timeout=3))
print("  客户端：300 条连接已建立")
time.sleep(2.0)
print("  客户端：已保持 2 秒（空闲超时 3 秒，此刻应仍在线）")
time.sleep(4.0)
dead = 0
for s in socks:
    s.settimeout(0.2)
    try:
        if s.recv(1) == b"":
            dead += 1
    except (TimeoutError, socket.timeout):
        pass
    except OSError:
        dead += 1
print(f"  客户端：等待到 6 秒，其中 {dead}/300 已被服务端断开")
for s in socks:
    s.close()
EOF
CLIENT_PID=$!

sleep 1.2
echo "建立 300 条后 fd = $(count_fds)"
sleep 4.5
echo "空闲超时后（服务端应已回收）fd = $(count_fds)"
wait $CLIENT_PID
sleep 1
echo "客户端全部退出后 fd = $(count_fds)"
