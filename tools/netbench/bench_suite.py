#!/usr/bin/env python3
"""C7 基准套件：按设计文档 M10 的四组条件跑 `bench_client`，输出一张表。

用法（先在一个终端里把服务跑起来，例如 `{ sleep 300; echo exit; } | ./InkingBackendFramework`）：

    python tools/netbench/bench_suite.py --bin build/bin
    python tools/netbench/bench_suite.py --bin build/bin --repeat 2
    python tools/netbench/bench_suite.py --bin build/bin --only size

为什么要 `--repeat 2`：M10 的指标是「连续两次运行偏差 < 5%」。单跑一次的数字
没法区分「框架快」和「这台机器那会儿正好闲」，两次一比就露馅了。

三组的自变量（对应 M10 的「消息大小 × 业务耗时 × 连接数」）：

    size   消息大小 64 / 256 / 1024 / 4096 字节，连接数固定 64
    conns  连接数 1 / 8 / 32 / 64 / 256，消息固定 256 字节
    cost   业务耗时 0 / 100 / 1000 µs（需要服务端配置 enableBenchCommands=true）

每条用例都从 `bench_client` 的 RESULT 行解析，客户端与服务端的字段一一对应。
"""

import argparse
import os
import re
import subprocess
import sys

for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

RESULT_RE = re.compile(
    r"RESULT .*?ok=(?P<ok>\d+) fail=(?P<fail>\d+) elapsed_s=(?P<elapsed>[\d.]+) "
    r"throughput=(?P<throughput>\d+) p50_us=(?P<p50>[\d.]+) p90_us=(?P<p90>[\d.]+) "
    r"p99_us=(?P<p99>[\d.]+) p999_us=(?P<p999>[\d.]+) max_us=(?P<max>[\d.]+)"
)


def exe_name(name: str) -> str:
    return name + (".exe" if os.name == "nt" else "")


def run_case(binary: str, args: list) -> dict:
    command = [binary] + [str(item) for item in args]
    completed = subprocess.run(command, capture_output=True, text=True, timeout=600)
    for line in completed.stdout.splitlines():
        match = RESULT_RE.search(line)
        if match:
            data = {key: float(value) for key, value in match.groupdict().items()}
            data["ok"] = int(data["ok"])
            data["fail"] = int(data["fail"])
            data["throughput"] = int(data["throughput"])
            return data
    return {"error": completed.stdout.strip() + " " + completed.stderr.strip()}


def fmt_row(label: str, data: dict, previous: dict = None) -> str:
    if "error" in data:
        return f"{label:<26} 失败：{data['error'][:80]}"
    delta = ""
    if previous and previous.get("throughput"):
        change = (data["throughput"] - previous["throughput"]) / previous["throughput"] * 100.0
        delta = f" {change:+5.1f}%"
    return (
        f"{label:<26} {data['throughput']:>9,} 条/秒{delta:<8} "
        f"p50 {data['p50']:>8.1f}  p90 {data['p90']:>8.1f}  "
        f"p99 {data['p99']:>8.1f}  p99.9 {data['p999']:>8.1f}  "
        f"max {data['max']:>8.1f} us"
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bin", default="build/bin", help="bench_client 所在目录")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9000)
    parser.add_argument("--frames", type=int, default=2000, help="每条连接发多少帧")
    parser.add_argument("--warmup", type=int, default=200)
    parser.add_argument("--repeat", type=int, default=1, help="每组跑几遍（M10 要求 2 遍看偏差）")
    parser.add_argument("--only", default="all", choices=["all", "size", "conns", "cost"])
    args = parser.parse_args()

    binary = os.path.join(args.bin, exe_name("bench_client"))
    if not os.path.exists(binary):
        print(f"找不到 {binary}：先编译（cmake --build build）")
        return 2

    common = ["--host", args.host, "--port", args.port, "--frames", args.frames, "--warmup", args.warmup]

    groups = []
    if args.only in ("all", "size"):
        groups.append(("消息大小（连接数 64）", [
            ("64 B", ["--conns", 64, "--payload", 64]),
            ("256 B", ["--conns", 64, "--payload", 256]),
            ("1 KiB", ["--conns", 64, "--payload", 1024]),
            ("4 KiB", ["--conns", 64, "--payload", 4096]),
        ]))
    if args.only in ("all", "conns"):
        groups.append(("连接数（消息 256 B）", [
            ("1 条", ["--conns", 1, "--payload", 256]),
            ("8 条", ["--conns", 8, "--payload", 256]),
            ("32 条", ["--conns", 32, "--payload", 256]),
            ("64 条", ["--conns", 64, "--payload", 256]),
            ("256 条", ["--conns", 256, "--payload", 256]),
        ]))
    if args.only in ("all", "cost"):
        groups.append(("业务耗时（连接数 64，256 B）", [
            ("0 us（echo）", ["--conns", 64, "--payload", 256]),
            ("100 us", ["--conns", 64, "--payload", 256, "--command", "bench-sleep", "--sleep-us", 100]),
            ("1000 us", ["--conns", 64, "--payload", 256, "--command", "bench-sleep", "--sleep-us", 1000]),
        ]))

    print(f"bench_client: {binary}")
    print(f"每条连接 {args.frames} 帧（预热 {args.warmup}），重复 {args.repeat} 遍\n")

    failures = 0
    for title, cases in groups:
        print(f"== {title} ==")
        for label, case_args in cases:
            previous = None
            for round_index in range(args.repeat):
                data = run_case(binary, common + case_args)
                if "error" in data or data["fail"] > 0:
                    failures += 1
                suffix = "" if args.repeat == 1 else f"（第 {round_index + 1} 遍）"
                print("  " + fmt_row(label + suffix, data, previous if round_index > 0 else None))
                if "error" not in data:
                    previous = data
        print()

    if failures:
        print(f"有 {failures} 次运行失败或不完整（见上面的错误行）")
    else:
        print("全部运行完成，无失败请求")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
