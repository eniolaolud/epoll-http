#!/usr/bin/env python3
import os

print("Content-Type: text/html; charset=utf-8")
print("Status: 200 OK")
print()
keys = [
    "REQUEST_METHOD",
    "QUERY_STRING",
    "CONTENT_LENGTH",
    "CONTENT_TYPE",
    "SCRIPT_NAME",
    "HTTP_COOKIE",
    "HTTP_HOST",
    "SERVER_SOFTWARE",
]
print("<!doctype html><html><head><meta charset=utf-8><title>CGI env</title>")
print('<link rel="stylesheet" href="/style.css"></head><body>')
print('<header class="hero"><p class="brand">epoll-http</p>')
print("<h1>CGI environment</h1>")
print('<p class="lede">Values exported to the CGI process.</p></header>')
print("<main><section><ul>")
for k in keys:
    v = os.environ.get(k, "")
    print(f"<li><strong>{k}</strong> = <code>{v}</code></li>")
print('</ul><p><a href="/">Home</a></p></section></main></body></html>')
