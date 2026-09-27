"""接口测试夹具：启动真实的 webserver 二进制，测试结束再关闭。

设计取舍：用真实进程而不是进程内调用，能覆盖 epoll、线程模型与关闭路径；
代价是要自己处理端口占用与启动时序（下面用轮询等待端口就绪）。
"""

import os
import socket
import subprocess
import time

import pytest

BINARY = os.environ.get("MYSELF_WEBSERVER_BIN", "./build/webserver")
HOST = "127.0.0.1"
PORT = int(os.environ.get("MYSELF_TEST_PORT", "18080"))


def _wait_until_listening(timeout: float = 10.0) -> bool:
    deadline = time.time() + timeout
    while time.time() < deadline:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
            probe.settimeout(0.2)
            if probe.connect_ex((HOST, PORT)) == 0:
                return True
        time.sleep(0.1)
    return False


@pytest.fixture(scope="session")
def base_url() -> str:
    if not os.path.exists(BINARY):
        pytest.skip(f"server binary not found: {BINARY}（先执行 cmake --build build）")

    proc = subprocess.Popen(
        [BINARY, "-p", str(PORT), "-t", "2", "-l", "warn", "-q"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )

    if not _wait_until_listening():
        proc.terminate()
        pytest.fail("server did not start listening in time")

    yield f"http://{HOST}:{PORT}"

    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
