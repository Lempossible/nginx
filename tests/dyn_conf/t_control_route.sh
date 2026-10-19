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

out=$(ctl /1/control/dynamic)
assert_contains "$out" "200" "GET /1/control/dynamic returns 200"
assert_contains "$out" "generation" "status reports generation"

out=$(ctl_patch /1/control/dynamic)
assert_contains "$out" "200" "PATCH /1/control/dynamic returns 200"
assert_log_contains "dyn_conf: validate ok" "patch triggers validation"

stop_nginx
report control_route
