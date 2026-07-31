#!/usr/bin/env python3
"""Serve the PicoPro browser UI simulator."""

from __future__ import annotations

import http.server
import json
import socketserver
import webbrowser
from functools import partial
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PORT = 8765


class PicoProSimHandler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self) -> None:
        if self.path.split("?", 1)[0] == "/sims/fonts.json":
            fonts = []
            for path in sorted((ROOT / "Fonts").glob("*.h"), key=lambda p: p.name.lower()):
                fonts.append({
                    "name": path.stem,
                    "path": f"../Fonts/{path.name}",
                    "current": False,
                })
            payload = json.dumps({"fonts": fonts}, indent=2).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
            return
        super().do_GET()


def main() -> None:
    handler = partial(PicoProSimHandler, directory=str(ROOT))
    with socketserver.TCPServer(("127.0.0.1", PORT), handler) as server:
        url = f"http://127.0.0.1:{PORT}/sims/index.html"
        print(f"PicoPro UI simulator: {url}")
        webbrowser.open(url)
        server.serve_forever()


if __name__ == "__main__":
    main()
