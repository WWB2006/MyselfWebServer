#!/usr/bin/env python3
"""
MyselfWebServer Windows Preview & Mock Server
Serves www/ on http://127.0.0.1:8080 with mock /api/status and /metrics for Windows interactive preview.
"""

import sys
import json
import time
import webbrowser
from pathlib import Path
from http.server import HTTPServer, SimpleHTTPRequestHandler

PROJECT_ROOT = Path(__file__).resolve().parent.parent
WWW_DIR = PROJECT_ROOT / "www"

START_TIME = time.time()
REQUEST_COUNT = 0

class WindowsWebserverHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(WWW_DIR), **kwargs)

    def log_message(self, format, *args):
        # Mute console noise
        pass

    def do_GET(self):
        global REQUEST_COUNT
        REQUEST_COUNT += 1

        if self.path == "/api/status":
            uptime = int(time.time() - START_TIME)
            data = {
                "server": "MyselfWebServer/1.0 (Windows Preview)",
                "status": "online",
                "total_requests": REQUEST_COUNT,
                "active_connections": 4,
                "uptime_seconds": uptime,
                "concurrency_engine": "Epoll Reactor (Emulated on Windows)"
            }
            body = json.dumps(data).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        if self.path == "/metrics":
            uptime = int(time.time() - START_TIME)
            lines = [
                "# HELP http_requests_total Total number of HTTP requests processed",
                "# TYPE http_requests_total counter",
                f"http_requests_total {REQUEST_COUNT}",
                "# HELP http_connections_active Current active connections",
                "# TYPE http_connections_active gauge",
                "http_connections_active 4",
                "# HELP server_uptime_seconds Server uptime in seconds",
                "# TYPE server_uptime_seconds counter",
                f"server_uptime_seconds {uptime}"
            ]
            body = ("\n".join(lines) + "\n").encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/plain; version=0.0.4; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        if self.path == "/health":
            body = b'{"status":"UP","engine":"reactor"}'
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        super().do_GET()

def main():
    port = 8080
    server_address = ("0.0.0.0", port)
    httpd = HTTPServer(server_address, WindowsWebserverHandler)
    print("================================================================")
    print(f" MyselfWebServer Preview Server listening on port {port}")
    print(f" Web Root: {WWW_DIR}")
    print(f" URL: http://127.0.0.1:{port}")
    print("================================================================")

    try:
        webbrowser.open(f"http://127.0.0.1:{port}")
    except Exception:
        pass

    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nServer stopped.")

if __name__ == "__main__":
    main()
