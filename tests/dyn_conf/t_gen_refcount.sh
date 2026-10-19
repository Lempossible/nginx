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
    keepalive_timeout 2s;
    server {
        listen 18080;
        location / { root html; }
    }
}
EOF

start_nginx || exit 1

assert_eq "$(http_code /)" "200" "request served with gen capture"

for _ in $(seq 1 5); do sleep 0.2; curl -s -o /dev/null "http://127.0.0.1:$PORT/"; done

stop_nginx
sleep 0.5

assert_log_contains "dyn_conf: cycle cleanup gen=0" \
    "cycle cleanup unrefs the generation"

report gen_refcount
