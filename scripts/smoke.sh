#!/usr/bin/env bash
# Smoke-test epoll-http. Usage: ./scripts/smoke.sh [base_url]
set -euo pipefail
BASE="${1:-http://127.0.0.1:38491}"
CJ="$(mktemp)"
trap 'rm -f "$CJ"' EXIT

echo "== GET / =="
curl -sfS -o /dev/null -w "%{http_code}\n" "$BASE/" | grep -q 200

echo "== HTTP/1.1 requires Host =="
port="${BASE##*:}"
host_code="$(python3 - "$port" <<'PY'
import socket, sys
port = int(sys.argv[1])
s = socket.create_connection(("127.0.0.1", port), 2)
s.sendall(b"GET / HTTP/1.1\r\nConnection: close\r\n\r\n")
line = s.recv(128).decode("latin1", "replace").split("\r\n", 1)[0]
parts = line.split()
print(parts[1] if len(parts) >= 2 else "")
s.close()
PY
)"
[[ "$host_code" == "400" ]]

echo "== HEAD / =="
code="$(curl -sS -o /dev/null -w '%{http_code}' -I "$BASE/")"
[[ "$code" == "200" ]]

echo "== cookies =="
curl -sfS -c "$CJ" -b "$CJ" -X POST -d 'name=demo&value=42' "$BASE/api/cookies" -D - -o /dev/null \
  | grep -qi 'set-cookie: demo=42'
curl -sfS -b "$CJ" "$BASE/api/cookies" | grep -q demo

echo "== upload / download / delete =="
echo 'smoke-body' | curl -sfS -X POST --data-binary @- "$BASE/api/upload?name=smoke.txt" >/dev/null
curl -sfS "$BASE/uploads/smoke.txt" | grep -q smoke-body
# Downloads under /uploads/ should advertise Content-Disposition
curl -sS -D - -o /dev/null "$BASE/uploads/smoke.txt" | grep -qi 'content-disposition: attachment'
curl -sfS -X DELETE "$BASE/api/delete?name=smoke.txt" >/dev/null
code="$(curl -sS -o /dev/null -w '%{http_code}' "$BASE/uploads/smoke.txt")"
[[ "$code" == "404" ]]

echo "== multipart upload =="
mp="$(mktemp)"
echo 'multipart-payload' >"$mp"
curl -sfS -F "file=@${mp};filename=multi.txt" "$BASE/api/upload" >/dev/null
curl -sfS "$BASE/uploads/multi.txt" | grep -q multipart-payload
curl -sfS -X DELETE "$BASE/api/delete?name=multi.txt" >/dev/null
rm -f "$mp"

echo "== CGI =="
curl -sfS "$BASE/cgi-bin/hello.sh" | grep -qi 'Hello from CGI'
curl -sfS -X POST -d 'message=hi' "$BASE/cgi-bin/echo.py" | grep -q hi

echo "== path traversal blocked =="
for path in '/../../etc/passwd' '/uploads/../etc/passwd' '/%2e%2e/etc/passwd'; do
  code="$(curl -sS -o /dev/null -w '%{http_code}' "$BASE$path")"
  [[ "$code" == "403" || "$code" == "404" || "$code" == "400" ]]
done

echo "== concurrent GETs =="
tmpdir="$(mktemp -d)"
trap 'rm -f "$CJ"; rm -rf "$tmpdir"' EXIT
for i in $(seq 1 40); do
  curl -sfS -o "$tmpdir/$i.body" -w "%{http_code}" "$BASE/" >"$tmpdir/$i.code" &
done
wait
ok=0
for i in $(seq 1 40); do
  [[ "$(cat "$tmpdir/$i.code")" == "200" ]] || { echo "FAIL concurrent $i"; exit 1; }
  ok=$((ok + 1))
done
echo "ok $ok/40"

echo "ALL_SMOKE_OK ($BASE)"
