#!/bin/sh
. "$(dirname "$0")/harness.sh"

build_nginx || { echo "build failed"; exit 1; }

make_prefix
write_conf <<'EOF'
worker_processes 2;
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
assert_eq "$(http_code /)" "200" "serving before dispatch"

ctl_patch /1/control/dynamic > /dev/null
sleep 1

assert_log_contains "dyn_conf: worker built gen=1" "worker rebuilt generation on channel command"
assert_eq "$(grep -c 'dyn_conf: worker built gen=1' "$LOG")" "2" \
    "both workers rebuilt"

report channel_dispatch
