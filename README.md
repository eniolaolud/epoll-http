# epoll-http

A small **non-blocking HTTP/1.1 server** in C++17 for Linux. It uses `epoll` (edge-triggered) so many clients can read and write concurrently without one slow connection blocking others.

## Features

- **Methods:** GET, HEAD, POST, DELETE, OPTIONS
- **Static files** from `www/` (MIME types, path traversal checks)
- **Cookies:** parse `Cookie`, send `Set-Cookie` via `/api/cookies`
- **Uploads:** multipart or raw POST to `/api/upload` → `www/uploads/`
- **Downloads:** GET `/uploads/<file>`
- **DELETE:** `/api/delete?name=...` or `DELETE /uploads/<file>`
- **CGI:** fork/exec scripts under `cgi-bin/` with standard CGI environment variables

Default listen address: **0.0.0.0:38491**.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Binary: `build/http_server`.

## Run

From the repository root (so relative `www` and `cgi-bin` resolve):

```bash
./build/http_server --port 38491 --www www --cgi cgi-bin
```

Options: `--host`, `--port`, `--www`, `--cgi`.

Open [http://127.0.0.1:38491](http://127.0.0.1:38491).

## Demo pages & scripts

| Path | What it shows |
|------|----------------|
| `/` | Demo UI: static, cookies, upload, delete, CGI links |
| `/about.html` | Static HTML |
| `/api/cookies` | Inspect / set cookies |
| `/api/upload` | Multipart file upload |
| `/api/uploads` | JSON list of uploads |
| `/api/delete?name=` | Delete an upload |
| `/cgi-bin/hello.sh` | Bash CGI |
| `/cgi-bin/env.py` | Python CGI (env + query) |
| `/cgi-bin/echo.py` | Python CGI POST echo |

## Smoke tests

```bash
curl -sS http://127.0.0.1:38491/ | head
curl -sS -c /tmp/cj -b /tmp/cj -X POST -d 'name=demo&value=42' http://127.0.0.1:38491/api/cookies -D -
echo hello | curl -sS -X POST --data-binary @- 'http://127.0.0.1:38491/api/upload?name=hello.txt'
curl -sS http://127.0.0.1:38491/uploads/hello.txt
curl -sS -X DELETE 'http://127.0.0.1:38491/api/delete?name=hello.txt'
curl -sS 'http://127.0.0.1:38491/cgi-bin/hello.sh'
curl -sS -X POST -d 'message=hi' http://127.0.0.1:38491/cgi-bin/echo.py
```

## Caveats

- **Linux-only** (`epoll`, `accept4`).
- CGI runs synchronously in the request path (fork/exec + read stdout); a slow CGI can delay that connection briefly. Client socket I/O remains non-blocking.
- Upload body size capped at 32 MiB; no chunked transfer encoding.
- Intended as a teaching / demo server, not a hardened production reverse proxy.
