"""令牌桶限流与 429 优雅降级测试"""

import os
import subprocess
import time
import requests
import pytest

BINARY = os.environ.get("MYSELF_WEBSERVER_BIN", "./build/webserver")
RATE_PORT = 18082


@pytest.fixture(scope="module")
def rate_limited_server():
    if not os.path.exists(BINARY):
        pytest.skip(f"server binary not found: {BINARY}")

    # 启动速率限制为 5 QPS，突发容量 3 的实例
    proc = subprocess.Popen(
        [BINARY, "-p", str(RATE_PORT), "-t", "2", "-R", "5", "-B", "3", "-l", "warn", "-q"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    time.sleep(0.4)
    yield f"http://127.0.0.1:{RATE_PORT}"

    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()


def test_rate_limiting_returns_429_with_retry_after(rate_limited_server):
    url = f"{rate_limited_server}/index.html"

    # 突发连续发送 10 个请求
    statuses = []
    for _ in range(10):
        resp = requests.get(url, timeout=2.0)
        statuses.append((resp.status_code, resp.headers.get("Retry-After")))

    # 前面突发允许通过（200），后续超额被 429 拦截
    status_codes = [s[0] for s in statuses]
    assert 200 in status_codes
    assert 429 in status_codes

    # 429 响应头必须携带 Retry-After
    for code, retry_after in statuses:
        if code == 429:
            assert retry_after == "1"


def test_management_paths_bypass_rate_limiting(rate_limited_server):
    # 监控接口 /api/status 与 /metrics 白名单放行
    for _ in range(10):
        resp = requests.get(f"{rate_limited_server}/metrics", timeout=2.0)
        assert resp.status_code == 200
        resp_status = requests.get(f"{rate_limited_server}/api/status", timeout=2.0)
        assert resp_status.status_code == 200
