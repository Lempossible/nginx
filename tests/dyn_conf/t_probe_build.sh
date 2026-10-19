#!/bin/sh
. "$(dirname "$0")/harness.sh"

build_nginx || { echo "build failed"; exit 1; }

make_prefix
write_conf <<'EOF'
worker_processes 1;
error_log logs/error.log notice;
pid logs/nginx.pid;
events { worker_connections 64; }
http {
    access_log off;
    limit_req_zone $binary_remote_addr zone=rl:1m rate=10r/s;
    server {
        listen 18080;
        server_name a.example.com;
        location / { root html; limit_req zone=rl burst=5; }
    }
}
EOF

start_nginx || exit 1

assert_eq "$(http_code /)" "200" "http works before probe build"

assert_eq "$(grep -c 'dyn_conf: probe build ok' "$LOG" 2>/dev/null)" "0" \
    "probe build not run on initial start"
assert_eq "$(grep -c 'probe build failed' "$LOG" 2>/dev/null)" "0" \
    "no probe build failure on initial start"

"$NGX_BIN" -p "$PREFIX" -c "$PREFIX/conf/nginx.conf" -s reload >/dev/null 2>&1
sleep 1

assert_eq "$(grep -c 'dyn_conf: probe build ok' "$LOG" 2>/dev/null)" "1" \
    "probe build runs once on reload path"
assert_eq "$(grep -c 'probe build failed' "$LOG" 2>/dev/null)" "0" \
    "probe build did not fail"
assert_eq "$(http_code /)" "200" "http works after probe build"

codes=""
for _ in $(seq 1 20); do codes="$codes $(http_code /)"; done
assert_contains "$codes" "503" "limit_req still enforced after probe build"

stop_nginx
report probe_build
