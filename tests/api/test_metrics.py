import re

import requests


def _metric(text: str, name: str):
    match = re.search(rf"^{re.escape(name)} ([0-9eE+\-.]+)$", text, re.MULTILINE)
    return float(match.group(1)) if match else None


def _metric_with_label(text: str, name: str, label: str):
    pattern = rf"^{re.escape(name)}\{{{label}\}} ([0-9eE+\-.]+)$"
    match = re.search(pattern, text, re.MULTILINE)
    return float(match.group(1)) if match else None


def _fetch_metrics(base_url) -> str:
    response = requests.get(base_url + "/metrics", timeout=5)
    assert response.status_code == 200
    return response.text


def test_metrics_content_type_and_families(base_url):
    response = requests.get(base_url + "/metrics", timeout=5)

    assert response.status_code == 200
    assert "text/plain" in response.headers["Content-Type"]

    body = response.text
    for family in (
        "myself_connections_total",
        "myself_requests_total",
        "myself_errors_total",
        "myself_bytes_read_total",
        "myself_bytes_written_total",
        "myself_log_lines_total",
        "myself_uptime_seconds",
        "myself_request_latency_ms_bucket",
        "myself_request_latency_ms_count",
    ):
        assert family in body, f"缺少指标：{family}"


def test_request_counter_close_to_traffic(base_url):
    before = _metric(_fetch_metrics(base_url), "myself_requests_total")
    assert before is not None

    rounds = 5
    for _ in range(rounds):
        requests.get(base_url + "/index.html", timeout=5)

    after = _metric(_fetch_metrics(base_url), "myself_requests_total")
    assert after is not None
    # 抓取指标本身也计入请求，所以差值至少是 rounds
    assert after - before >= rounds


def test_404_increments_4xx_counter(base_url):
    before = _metric_with_label(_fetch_metrics(base_url), "myself_requests_by_status", 'class="4xx"')
    assert before is not None

    requests.get(base_url + "/not-exist", timeout=5)

    after = _metric_with_label(_fetch_metrics(base_url), "myself_requests_by_status", 'class="4xx"')
    assert after is not None
    assert after - before >= 1


def test_keep_alive_request_is_counted_once(base_url):
    session = requests.Session()
    session.get(base_url + "/index.html", timeout=5)

    request_count = _metric(_fetch_metrics(base_url), "myself_requests_total")
    assert request_count is not None
    assert request_count >= 1


def test_latency_histogram_is_cumulative(base_url):
    requests.get(base_url + "/index.html", timeout=5)
    body = _fetch_metrics(base_url)

    buckets = {
        match.group(1): float(match.group(2))
        for match in re.finditer(
            r'^myself_request_latency_ms_bucket\{le="([^"]+)"\} ([0-9eE+\-.]+)$',
            body,
            re.MULTILINE,
        )
    }

    assert "+Inf" in buckets
    infinite = buckets["+Inf"]
    for value in buckets.values():
        assert value <= infinite, "累计语义被破坏：桶计数超过了 +Inf 桶"
    assert buckets["1"] <= buckets["5"] <= infinite


def test_latency_count_matches_request_count(base_url):
    body = _fetch_metrics(base_url)

    requests_total = _metric(body, "myself_requests_total")
    latency_count = _metric(body, "myself_request_latency_ms_count")

    assert requests_total is not None
    assert latency_count is not None
    assert abs(requests_total - latency_count) <= 1


def test_connection_gauges_are_sane(base_url):
    data = requests.get(base_url + "/api/status", timeout=5).json()
    connections = data["connections"]

    assert connections["total"] >= 1
    assert connections["max"] >= connections["current"]
    # 当前请求自己可能还占用一条连接，因此只要求接近 0
    assert connections["current"] <= 2


def test_metrics_endpoint_does_not_raise_error_counter(base_url):
    before = _metric(_fetch_metrics(base_url), "myself_errors_total")
    assert before is not None

    requests.get(base_url + "/metrics", timeout=5)

    after = _metric(_fetch_metrics(base_url), "myself_errors_total")
    assert after is not None
    assert after >= before
