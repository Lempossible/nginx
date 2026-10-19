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
    server {
        listen 18080;
        location / { root html; }
    }
}
EOF

start_nginx || exit 1

assert_eq "$(http_body /)" "static-root" "nginx serves static file"
assert_contains "$(ctl /1/nginx)" "version" "control API reachable"
assert_contains "$(ctl /1/control/config)" "name" \
    "control config endpoint reachable"

stop_nginx
report smoke
