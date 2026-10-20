"""sendfile(2) 零拷贝传输验证：内容完整性、HEAD、pipelining 顺序、keep-alive 复用。

测试自己生成一个 256KB 的静态文件（大于默认 64KB 阈值），跑完删除，
不依赖仓库里恰好存在大文件。
"""

import hashlib
import os
import socket

import pytest
import requests

SIZE = 256 * 1024
NAME = "_zero_copy_test.bin"
PAYLOAD = bytes(i % 251 for i in range(SIZE))
WWW_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "www"))


@pytest.fixture(scope="module", autouse=True)
def large_file():
    path = os.path.join(WWW_ROOT, NAME)
    with open(path, "wb") as handle:
        handle.write(PAYLOAD)
    try:
        yield path
    finally:
        if os.path.exists(path):
            os.remove(path)


def test_large_file_download_is_intact(base_url):
    response = requests.get(f"{base_url}/{NAME}", timeout=10)

    assert response.status_code == 200
    assert response.headers["Content-Length"] == str(SIZE)
    assert len(response.content) == SIZE
    assert hashlib.sha256(response.content).digest() == hashlib.sha256(PAYLOAD).digest()


def test_head_large_file_reports_length_without_body(base_url):
    response = requests.head(f"{base_url}/{NAME}", timeout=10)

    assert response.status_code == 200
    assert response.headers["Content-Length"] == str(SIZE)
    assert response.content == b""


def test_keep_alive_can_download_twice(base_url):
    session = requests.Session()
    for _ in range(2):
        response = session.get(f"{base_url}/{NAME}", timeout=10)
        assert response.status_code == 200
        assert response.content == PAYLOAD


def test_pipelined_large_then_small_keeps_order(base_url):
    """同一连接里连发两个请求：先大文件（走 sendfile）再小页面（走内存缓冲）。

    这条用例专门守 sendfile 的边界 bug：如果文件体插到了下一个响应前面，
    或者响应头被文件数据覆盖，这里立刻就会错位。
    """
    host, port = base_url.split("//", 1)[1].split(":")

    with socket.create_connection((host, int(port)), timeout=10) as sock:
        sock.sendall(
            f"GET /{NAME} HTTP/1.1\r\nHost: x\r\n\r\n".encode()
            + b"GET /index.html HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n"
        )
        chunks = []
        while True:
            data = sock.recv(65536)
            if not data:
                break
            chunks.append(data)

    raw = b"".join(chunks)
    assert raw.count(b"HTTP/1.1 200 OK") == 2

    first_head_end = raw.index(b"\r\n\r\n") + 4
    assert raw.startswith(b"HTTP/1.1 200 OK")
    assert raw[first_head_end:first_head_end + SIZE] == PAYLOAD

    rest = raw[first_head_end + SIZE:]
    assert rest.startswith(b"HTTP/1.1 200 OK"), "第二个响应必须排在文件体之后"
    assert b"<html" in rest.lower()
