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
    client_max_body_size 1m;
    server {
        listen 18080;
        server_name a.example.com;
        location / { root html; }
        location /b { root html; }
    }
}
EOF

start_nginx || exit 1
assert_eq "$(http_body /)" "static-root" "static file served with gp enabled"

stop_nginx
assert_log_contains "dyn_conf: gen=0 freed" "generation 0 pool destroyed with stats"

report gp_boot
