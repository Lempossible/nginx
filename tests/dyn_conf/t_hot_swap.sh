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
        server_name a.example.com;
        location / { root html; add_header X-Marker v1; }
        location /api { return 200 "api-v1\n"; }
    }
}
EOF

start_nginx || exit 1

assert_eq "$(curl -sI "http://127.0.0.1:$PORT/" | grep -c 'X-Marker: v1')" "1" "v1 header present"
assert_eq "$(http_body /api)" "api-v1" "v1 body"

write_conf <<'EOF'
worker_processes 1;
error_log logs/error.log notice;
pid logs/nginx.pid;
events { worker_connections 64; }
http {
    access_log off;
    server {
        listen 18080;
        server_name b.example.com;
        location / { root html; add_header X-Marker v2; }
        location /api { return 200 "api-v2\n"; }
        location /new { return 200 "new-location\n"; }
    }
}
EOF

ctl_patch /1/control/dynamic > /dev/null
sleep 1

assert_eq "$(curl -sI "http://127.0.0.1:$PORT/" | grep -c 'X-Marker: v2')" "1" \
    "add_header updated without reload"
assert_eq "$(http_body /api)" "api-v2" "return body updated"
assert_eq "$(http_body /new)" "new-location" "new location takes effect"
assert_eq "$(curl -s -o /dev/null -w '%{http_code}' -H 'Host: b.example.com' \
    "http://127.0.0.1:$PORT/")" "200" "new server_name resolves"

write_conf <<'EOF'
worker_processes 1;
error_log logs/error.log notice;
pid logs/nginx.pid;
events { worker_connections 64; }
http {
    access_log off;
    server {
        listen 18080;
        server_name b.example.com;
        location / { root html; add_header X-Marker v3; }
        location /api { return 200 "api-v3\n"; }
        location /new { return 200 "new-location\n"; }
    }
}
EOF

ctl_patch /1/control/dynamic > /dev/null
sleep 1

assert_eq "$(curl -sI "http://127.0.0.1:$PORT/" | grep -c 'X-Marker: v3')" "1" \
    "second hot swap applies"
assert_eq "$(http_body /api)" "api-v3" "second swap body updated"

report hot_swap
