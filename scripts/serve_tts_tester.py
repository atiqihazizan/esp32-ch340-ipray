#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Pelayan HTTP ringkas: hidangkan folder projek + laluan /tts yang memproksi Google Translate TTS.

Chrome menyekat audio Google secara langsung dari halaman lain (Cross-Origin Read Blocking).
Memuatkan MP3 dari sama-asal (/tts) mengelak masalah itu.

Penggunaan:
  cd repo ini
  python3 scripts/serve_tts_tester.py

Kemudian buka dalam pelayar:
  http://127.0.0.1:8765/index.html
"""

from __future__ import annotations

import os
import urllib.error
import urllib.parse
import urllib.request
from http.server import HTTPServer, SimpleHTTPRequestHandler

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORT = 8765


class TTSHandler(SimpleHTTPRequestHandler):
    def log_message(self, fmt: str, *args) -> None:
        print("[%s] %s %s" % (self.log_date_time_string(), self.address_string(), fmt % args))

    def do_GET(self) -> None:  # noqa: N802
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path == "/tts":
            qs = urllib.parse.parse_qs(parsed.query)
            q = (qs.get("q") or [""])[0]
            tl = (qs.get("tl") or ["en"])[0]
            if not q.strip():
                self.send_error(400, "parameter q diperlukan")
                return

            google_url = (
                "https://translate.google.com/translate_tts?ie=UTF-8&q="
                + urllib.parse.quote(q, safe="")
                + "&tl="
                + urllib.parse.quote(tl, safe="")
                + "&total=1&idx=0&textlen="
                + str(len(q))
                + "&client=dict-chrome-ex&prev=input"
            )
            req = urllib.request.Request(
                google_url,
                headers={
                    "User-Agent": (
                        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
                        "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"
                    ),
                },
            )
            try:
                with urllib.request.urlopen(req, timeout=25) as resp:
                    data = resp.read()
            except urllib.error.HTTPError as e:
                self.send_error(e.code, e.reason)
                return
            except Exception as e:
                self.send_error(502, str(e))
                return

            self.send_response(200)
            self.send_header("Content-Type", "audio/mpeg")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(data)
            return

        SimpleHTTPRequestHandler.do_GET(self)


def main() -> None:
    os.chdir(ROOT)
    httpd = HTTPServer(("127.0.0.1", PORT), TTSHandler)
    print("TTS tester (Google melalui proksi, elak CORB):")
    print("  http://127.0.0.1:%d/index.html" % PORT)
    print("Tekan Ctrl+C untuk berhenti.")
    httpd.serve_forever()


if __name__ == "__main__":
    main()
