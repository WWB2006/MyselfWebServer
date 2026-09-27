import socket

import requests


def test_index_returns_200_with_content_length(base_url):
    response = requests.get(base_url + "/index.html", timeout=5)

    assert response.status_code == 200
    assert "Content-Length" in response.headers
    assert response.text


def test_root_maps_to_index(base_url):
    assert requests.get(base_url + "/", timeout=5).status_code == 200


def test_missing_path_returns_404(base_url):
    response = requests.get(base_url + "/definitely-missing", timeout=5)

    assert response.status_code == 404
    assert "Content-Length" in response.headers


def test_head_request_has_no_body(base_url):
    response = requests.head(base_url + "/index.html", timeout=5)

    assert response.status_code == 200
    assert response.content == b""


def test_keep_alive_reuses_connection(base_url):
    session = requests.Session()
    for _ in range(3):
        assert session.get(base_url + "/index.html", timeout=5).status_code == 200


def test_status_endpoint_returns_expected_fields(base_url):
    data = requests.get(base_url + "/api/status", timeout=5).json()

    assert data["status"] == "ok"
    assert data["name"] == "MyselfWebServer"
    for field in ("requests", "connections", "latencyMs", "bytes", "logLines"):
        assert field in data


def test_raw_path_traversal_is_rejected(base_url):
    host, port = base_url.split("//", 1)[1].split(":")

    # requests 会自动规范化 URL，这里用原始 socket 发送带 ".." 的路径
    with socket.create_connection((host, int(port)), timeout=5) as sock:
        sock.sendall(b"GET /../etc/passwd HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
        response = sock.recv(4096)

    status_line = response.split(b"\r\n", 1)[0]
    assert b" 400 " in status_line or b" 404 " in status_line
