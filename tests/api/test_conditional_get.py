"""HTTP 条件请求（RFC 9110 §13）：ETag / Last-Modified / 304 Not Modified"""

import requests


def _validators(base_url):
    response = requests.get(base_url + "/index.html", timeout=5)
    assert response.status_code == 200
    return response.headers.get("ETag"), response.headers.get("Last-Modified")


def test_static_file_carries_etag_and_last_modified(base_url):
    etag, last_modified = _validators(base_url)

    assert etag, "静态响应必须带 ETag"
    assert etag.startswith('"') and etag.endswith('"'), "强校验器需要双引号"
    assert last_modified and last_modified.endswith("GMT")
    assert requests.get(base_url + "/index.html", timeout=5).headers["Cache-Control"] == "no-cache"


def test_if_none_match_returns_304_without_body(base_url):
    etag, _ = _validators(base_url)

    response = requests.get(
        base_url + "/index.html", headers={"If-None-Match": etag}, timeout=5
    )

    assert response.status_code == 304
    assert response.content == b""
    assert response.headers.get("ETag") == etag


def test_if_modified_since_returns_304(base_url):
    _, last_modified = _validators(base_url)

    response = requests.get(
        base_url + "/index.html", headers={"If-Modified-Since": last_modified}, timeout=5
    )

    assert response.status_code == 304
    assert response.content == b""


def test_head_conditional_request_returns_304(base_url):
    etag, _ = _validators(base_url)

    response = requests.head(
        base_url + "/index.html", headers={"If-None-Match": etag}, timeout=5
    )

    assert response.status_code == 304
    assert response.content == b""


def test_stale_etag_still_returns_full_body(base_url):
    response = requests.get(
        base_url + "/index.html", headers={"If-None-Match": '"stale-etag-0"'}, timeout=5
    )

    assert response.status_code == 200
    assert response.text


def test_not_modified_counter_is_exposed(base_url):
    etag, _ = _validators(base_url)
    # 先制造一次 304，再读指标，避免依赖用例执行顺序
    assert requests.get(
        base_url + "/index.html", headers={"If-None-Match": etag}, timeout=5
    ).status_code == 304

    text = requests.get(base_url + "/metrics", timeout=5).text
    line = next(
        (item for item in text.splitlines()
         if item.startswith("myself_requests_not_modified_total ")),
        None,
    )
    assert line is not None, "Prometheus 文本缺少 myself_requests_not_modified_total"
    assert int(line.split()[-1]) >= 1

    status = requests.get(base_url + "/api/status", timeout=5).json()
    assert status["requests"]["notModified"] >= 1
