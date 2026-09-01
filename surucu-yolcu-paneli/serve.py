#!/usr/bin/env python3
import http.server, socketserver, sys
os_dir = "/home/spectraloop/surucu-yolcu-paneli"
PORT = 80

class H(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **k):
        super().__init__(*a, directory=os_dir, **k)
    def end_headers(self):
        # Turkce icin UTF-8 zorla + onbellek kapat
        self.send_header("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0")
        self.send_header("Pragma", "no-cache")
        super().end_headers()
    def guess_type(self, path):
        t = super().guess_type(path)
        if t and t.startswith("text/") and "charset" not in t:
            t += "; charset=utf-8"
        if t == "text/html":
            t = "text/html; charset=utf-8"
        return t

class S(socketserver.ThreadingMixIn, http.server.HTTPServer):
    address_family = __import__("socket").AF_INET6
    daemon_threads = True

with S(("::", PORT), H) as httpd:
    print(f"serving {os_dir} on :{PORT}", flush=True)
    httpd.serve_forever()
