#!/usr/bin/env python3
import os
import sys
from urllib.parse import parse_qs

length = int(os.environ.get("CONTENT_LENGTH") or "0")
body = sys.stdin.read(length) if length > 0 else ""
fields = parse_qs(body, keep_blank_values=True)
message = fields.get("message", [body])[0]

print("Content-Type: text/html; charset=utf-8")
print("Status: 200 OK")
print()
print("<!doctype html><html><head><meta charset=utf-8><title>CGI echo</title>")
print('<link rel="stylesheet" href="/style.css"></head><body>')
print('<header class="hero"><p class="brand">epoll-http</p>')
print("<h1>CGI POST echo</h1>")
print(f'<p class="lede">Method {os.environ.get("REQUEST_METHOD", "?")}</p></header>')
print("<main><section>")
print(f"<p>Received message:</p><pre>{message}</pre>")
print('<p><a href="/">Home</a></p></section></main></body></html>')
