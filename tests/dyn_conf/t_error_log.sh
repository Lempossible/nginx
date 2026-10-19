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
        location / { root html; add_header X-Marker v1; }
    }
}
EOF

start_nginx || exit 1
assert_eq "$(http_code /)" "200" "serving before error_log change"

write_conf <<'EOF'
worker_processes 1;
error_log logs/error.log notice;
pid logs/nginx.pid;
events { worker_connections 64; }
http {
    access_log off;
    server {
        listen 18080;
        error_log logs/error2.log notice;
        location / { root html; add_header X-Marker v2; }
    }
}
EOF

ctl_patch /1/control/dynamic > /dev/null
sleep 1

assert_eq "$(curl -sI "http://127.0.0.1:$PORT/" | grep -c 'X-Marker: v2')" "1" \
    "server error_log change applied without reload"

http_code /missing > /dev/null
sleep 1

assert_contains "$(cat "$PREFIX/logs/error2.log" 2>/dev/null)" \
    "No such file or directory" "request errors use the new server error_log file"

assert_eq "$(grep -c 'No such file or directory' "$LOG" 2>/dev/null)" "0" \
    "main error_log file no longer receives these request errors"

report error_log
