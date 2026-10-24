#!/usr/bin/env bash
printf 'Content-Type: text/html; charset=utf-8\r\n'
printf 'Status: 200 OK\r\n'
printf '\r\n'
cat <<EOF
<!doctype html>
<html><head><meta charset=utf-8><title>CGI hello</title>
<link rel="stylesheet" href="/style.css"></head>
<body>
<header class="hero">
  <p class="brand">epoll-http</p>
  <h1>Hello from CGI</h1>
  <p class="lede">Script: hello.sh · method: ${REQUEST_METHOD:-?} · query: ${QUERY_STRING:-}</p>
</header>
<main><section><p><a href="/">Home</a></p></section></main>
</body></html>
EOF
