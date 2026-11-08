# epoll-http

A small **non-blocking HTTP/1.1 server** in C++17 for Linux. It uses `epoll` (edge-triggered) so many clients can read and write concurrently without one slow connection blocking others.

## Features

- **Methods:** GET, HEAD, POST, DELETE, OPTIONS
- **Non-blocking I/O:** edge-triggered epoll, partial read/write state machine, keep-alive + pipelining
- **Idle timeout:** closes quiet keep-alive sockets (default 60s; `--idle-timeout 0` disables)
- **Static files** from `www/` (MIME types, `safe_join` path traversal checks)
- **Cookies:** parse `Cookie`, send `Set-Cookie` via `/api/cookies`
- **Uploads:** multipart or raw POST to `/api/upload` → `www/uploads/`
- **Downloads:** GET `/uploads/<file>` with `Content-Disposition: attachment`
- **DELETE:** `/api/delete?name=...` or `DELETE /uploads/<file>`
- **CGI:** fork/exec scripts under `cgi-bin/` with standard CGI environment variables
- **HTTP hygiene:** requires `Host` on HTTP/1.1, sends `Date` / `Server` headers

Default listen address: **0.0.0.0:38491**.

## Build

```bash
make build
# or: cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
```

Binary: `build/http_server`.

## Run

From the repository root (so relative `www` and `cgi-bin` resolve):

```bash
make run
# or: ./build/http_server --port 38491 --www www --cgi cgi-bin --idle-timeout 60
```

Options: `--host`, `--port`, `--www`, `--cgi`, `--idle-timeout`.

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

## Tests

With the server running:

```bash
make test          # ctest (safe_join) + smoke
./scripts/smoke.sh
# or: ./scripts/smoke.sh http://127.0.0.1:38491
```

Smoke covers GET/HEAD/POST/DELETE, cookies, multipart upload, CGI, path traversal, concurrent GETs, keep-alive pipelining, and HTTP/1.1 Host enforcement.

## How it fits together

1. **`Server::run`** — `epoll_wait` loop; accept clients edge-triggered (`EPOLLIN|EPOLLOUT|EPOLLET`); reap idle keep-alive connections.
2. **`Connection`** — state machine for partial reads/writes, keep-alive, and pipelining.
3. **`finalize_request`** — cookies, urlencoded forms, multipart uploads.
4. **`handle_request`** — routes API, CGI, then static files via **`safe_join`** (realpath + root prefix check).

## Project layout

```
include/          public headers
src/              server, connection, HTTP parse/serialize, main
www/              static demo site + uploads/
cgi-bin/          sample CGI scripts
tests/            safe_join unit test
scripts/smoke.sh  end-to-end smoke suite
```

## Caveats

- **Linux-only** (`epoll`, `accept4`).
- CGI runs synchronously in the request path (fork/exec + read stdout); a slow CGI can delay that connection briefly. Client socket I/O remains non-blocking.
- Upload body size capped at 32 MiB; no chunked transfer encoding.
- Intended as a teaching / demo server, not a hardened production reverse proxy.
