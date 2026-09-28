#!/usr/bin/env python3
"""故障注入用例：用原始 socket 覆盖半包、粘包、慢连接、超长头/体与连接抖动。

用法：
    python3 scripts/fault_injection.py --host 127.0.0.1 --port 8080

退出码 0 表示全部通过，1 表示有失败用例（可直接接入 CI 或手工回归）。
"""

import argparse
import socket
import sys
import time


def read_response(sock: socket.socket, timeout: float = 5.0) -> bytes:
    sock.settimeout(timeout)
    chunks = []
    while True:
        try:
            data = sock.recv(4096)
        except socket.timeout:
            break
        if not data:
            break
        chunks.append(data)
        if b"\r\n\r\n" in b"".join(chunks) and len(chunks) > 0:
            # 头部已收齐，简单起见再读一小会
            time.sleep(0.05)
            try:
                more = sock.recv(65536)
                if more:
                    chunks.append(more)
            except socket.timeout:
                pass
            break
    return b"".join(chunks)


def status_line(response: bytes) -> str:
    if not response:
        return "<空响应>"
    return response.split(b"\r\n", 1)[0].decode("latin-1")


def scenario_partial_then_complete(host: str, port: int) -> bool:
    """半包：先发一半请求，稍后补齐，应正常返回 200。"""
    with socket.create_connection((host, port), timeout=5) as sock:
        sock.sendall(b"GET /index.html HTTP/1.1\r\nHost: x\r\n")
        time.sleep(0.2)
        sock.sendall(b"Connection: close\r\n\r\n")
        response = read_response(sock)
    return "200" in status_line(response)


def scenario_pipelined(host: str, port: int) -> bool:
    """粘包：一次发送两条请求，两条都应被处理。"""
    payload = (
        b"GET /index.html HTTP/1.1\r\nHost: x\r\nConnection: keep-alive\r\n\r\n"
        b"GET /index.html HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n"
    )
    with socket.create_connection((host, port), timeout=5) as sock:
        sock.sendall(payload)
        response = read_response(sock)
    return response.count(b"HTTP/1.1 200") >= 2


def scenario_slow_connection(host: str, port: int, idle_timeout: float) -> bool:
    """慢连接：连上不发数据，超过空闲超时应被服务端断开。"""
    with socket.create_connection((host, port), timeout=5) as sock:
        sock.settimeout(idle_timeout + 5)
        started = time.time()
        data = sock.recv(1)
        elapsed = time.time() - started
    return data == b"" and elapsed <= idle_timeout + 5


def scenario_oversized_header(host: str, port: int) -> bool:
    """超长请求头：应返回 4xx 并关闭，而不是崩溃。"""
    with socket.create_connection((host, port), timeout=5) as sock:
        sock.sendall(b"GET / HTTP/1.1\r\nX-Long: " + b"a" * 9000 + b"\r\n\r\n")
        response = read_response(sock)
    line = status_line(response)
    return "400" in line or "413" in line or "431" in line or "<空响应>" == line


def scenario_oversized_body(host: str, port: int) -> bool:
    """超长请求体：声明 2MB 长度，服务端应拒绝或关闭连接。"""
    with socket.create_connection((host, port), timeout=5) as sock:
        head = b"POST /api/data HTTP/1.1\r\nHost: x\r\nContent-Length: 2097152\r\n\r\n"
        sock.sendall(head + b"b" * 4096)
        response = read_response(sock, timeout=3.0)
    line = status_line(response)
    return "400" in line or "413" in line or line == "<空响应>"

def scenario_connection_churn(host: str, port: int, rounds: int = 200) -> bool:
    """连接抖动：快速建立并断开，服务应存活且不泄漏 fd。"""
    for _ in range(rounds):
        try:
            with socket.create_connection((host, port), timeout=5) as sock:
                sock.sendall(b"GET /api/status HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
                read_response(sock, timeout=2.0)
        except (ConnectionResetError, BrokenPipeError):
            # 快速连断时对端可能发 RST，属于预期现象
            continue
    # 还能正常服务即视为通过
    with socket.create_connection((host, port), timeout=5) as sock:
        sock.sendall(b"GET /api/status HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
        return b"200" in read_response(sock)

def main() -> int:
    parser = argparse.ArgumentParser(description="MyselfWebServer 故障注入用例")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--idle-timeout", type=float, default=0.0,
                        help="服务端 -i 参数值；为 0 表示未启用空闲超时，慢连接用例会被跳过")
    args = parser.parse_args()

    cases = [
        ("半包请求", lambda: scenario_partial_then_complete(args.host, args.port)),
        ("粘包请求", lambda: scenario_pipelined(args.host, args.port)),
        ("超长请求头", lambda: scenario_oversized_header(args.host, args.port)),
        ("超长请求体", lambda: scenario_oversized_body(args.host, args.port)),
        ("连接抖动 200 次", lambda: scenario_connection_churn(args.host, args.port)),
    ]
    if args.idle_timeout > 0:
        cases.insert(2, ("慢连接超时",
                         lambda: scenario_slow_connection(args.host, args.port,
                                                          args.idle_timeout)))
    else:
        print("提示：未指定 --idle-timeout，跳过慢连接用例（服务端需用 -i 参数启用）")

    failures = 0
    print(f"{'场景':<18}{'结果':<8}说明")
    for name, runner in cases:
        try:
            ok = runner()
        except Exception as exc:  # noqa: BLE001 - 脚本需要给出可读失败原因
            ok = False
            print(f"{name:<18}{'FAIL':<8}{type(exc).__name__}: {exc}")
            failures += 1
            continue
        print(f"{name:<18}{'PASS' if ok else 'FAIL':<8}")
        if not ok:
            failures += 1

    print()
    print(f"合计：{len(cases)} 个场景，失败 {failures} 个")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
