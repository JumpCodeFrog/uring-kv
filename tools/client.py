#!/usr/bin/env python3
"""Small TCP client for exercising the uring-kv text protocol."""

from __future__ import annotations

import argparse
import socket
import sys
import time
from typing import Iterable, Sequence


class LineClient:
    def __init__(self, sock: socket.socket) -> None:
        self._sock = sock
        self._buffer = bytearray()

    def send_command(self, command: str) -> None:
        line = command.rstrip("\r\n") + "\r\n"
        self._sock.sendall(line.encode("utf-8"))

    def send_commands(self, commands: Sequence[str]) -> None:
        payload = "".join(command.rstrip("\r\n") + "\r\n" for command in commands)
        self._sock.sendall(payload.encode("utf-8"))

    def read_response(self) -> str:
        while True:
            marker = self._buffer.find(b"\r\n")
            if marker >= 0:
                line = self._buffer[:marker]
                del self._buffer[: marker + 2]
                return line.decode("utf-8", errors="replace")

            chunk = self._sock.recv(65536)
            if not chunk:
                raise ConnectionError("server closed the connection")
            self._buffer.extend(chunk)


def send_command(sock: socket.socket, command: str) -> str:
    client = LineClient(sock)
    client.send_command(command)
    return client.read_response()


def interactive(sock: socket.socket) -> int:
    for raw in sys.stdin:
        command = raw.strip()
        if not command:
            continue
        try:
            print(send_command(sock, command), flush=True)
        except (ConnectionError, OSError) as error:
            print(f"client: {error}", file=sys.stderr)
            return 1
    return 0


def run_commands(sock: socket.socket, commands: Iterable[str]) -> int:
    try:
        for command in commands:
            print(send_command(sock, command), flush=True)
    except (ConnectionError, OSError) as error:
        print(f"client: {error}", file=sys.stderr)
        return 1
    return 0


def percentile(values: Sequence[float], percent: float) -> float:
    if not values:
        return 0.0
    if len(values) == 1:
        return values[0]

    index = (len(values) - 1) * percent
    lower = int(index)
    upper = min(lower + 1, len(values) - 1)
    fraction = index - lower
    return values[lower] * (1.0 - fraction) + values[upper] * fraction


def print_benchmark_summary(
    operation: str,
    requests: int,
    pipeline: int,
    elapsed: float,
    latencies_ms: Sequence[float],
) -> None:
    sorted_latencies = sorted(latencies_ms)
    rate = requests / elapsed if elapsed > 0 else float("inf")
    p50 = percentile(sorted_latencies, 0.50)
    p99 = percentile(sorted_latencies, 0.99)

    print()
    print("Benchmark summary")
    print("+----------------+----------------+")
    print(f"| {'operation':<14} | {operation:<14} |")
    print(f"| {'requests':<14} | {requests:<14} |")
    print(f"| {'pipeline':<14} | {pipeline:<14} |")
    print(f"| {'total_sec':<14} | {elapsed:<14.6f} |")
    print(f"| {'req_per_sec':<14} | {rate:<14.2f} |")
    print(f"| {'p50_ms':<14} | {p50:<14.3f} |")
    print(f"| {'p99_ms':<14} | {p99:<14.3f} |")
    print("+----------------+----------------+")


def make_benchmark_commands(operation: str, start: int, count: int) -> list[str]:
    if operation == "SET":
        return [f"SET bench:{index} value:{index}" for index in range(start, start + count)]
    if operation == "GET":
        return [f"GET bench:{index}" for index in range(start, start + count)]
    raise ValueError(f"unsupported benchmark operation: {operation}")


def prepopulate_for_get(client: LineClient, requests: int, pipeline: int) -> None:
    for start in range(0, requests, pipeline):
        count = min(pipeline, requests - start)
        commands = make_benchmark_commands("SET", start, count)
        client.send_commands(commands)
        for _ in commands:
            response = client.read_response()
            if response != "OK":
                raise RuntimeError(f"unexpected prepopulate response: {response!r}")


def benchmark(sock: socket.socket, requests: int, pipeline: int, operation: str) -> int:
    if requests <= 0:
        print("client: --requests must be positive", file=sys.stderr)
        return 2
    if pipeline <= 0:
        print("client: --pipeline must be positive", file=sys.stderr)
        return 2

    operation = operation.upper()
    client = LineClient(sock)
    if operation == "GET":
        prepopulate_for_get(client, requests, pipeline)

    latencies_ms: list[float] = []
    started = time.perf_counter_ns()

    for start in range(0, requests, pipeline):
        count = min(pipeline, requests - start)
        commands = make_benchmark_commands(operation, start, count)
        sent_at = time.perf_counter_ns()
        client.send_commands(commands)

        for offset in range(count):
            response = client.read_response()
            received_at = time.perf_counter_ns()
            if operation == "SET" and response != "OK":
                print(f"client: unexpected SET response: {response!r}", file=sys.stderr)
                return 1
            if operation == "GET" and response != f"value:{start + offset}":
                print(f"client: unexpected GET response: {response!r}", file=sys.stderr)
                return 1
            latencies_ms.append((received_at - sent_at) / 1_000_000.0)

    elapsed = (time.perf_counter_ns() - started) / 1_000_000_000.0
    print_benchmark_summary(operation, requests, pipeline, elapsed, latencies_ms)
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="uring-kv TCP client")
    parser.add_argument("--host", default="127.0.0.1", help="server host")
    parser.add_argument("--port", type=int, default=7777, help="server port")
    parser.add_argument(
        "--command",
        "-c",
        action="append",
        help="command to send; may be provided more than once",
    )
    parser.add_argument("--bench", action="store_true", help="run pipelined benchmark mode")
    parser.add_argument("--requests", type=int, default=100_000, help="benchmark request count")
    parser.add_argument("--pipeline", type=int, default=32, help="benchmark pipeline depth")
    parser.add_argument(
        "--bench-operation",
        choices=("SET", "GET"),
        default="SET",
        help="benchmark command type",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    with socket.create_connection((args.host, args.port)) as sock:
        if args.bench:
            return benchmark(sock, args.requests, args.pipeline, args.bench_operation)
        if args.command:
            return run_commands(sock, args.command)
        return interactive(sock)


if __name__ == "__main__":
    raise SystemExit(main())
