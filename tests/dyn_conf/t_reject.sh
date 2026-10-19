#!/bin/sh
. "$(dirname "$0")/harness.sh"

build_nginx || { echo "build failed"; exit 1; }

make_prefix

base_conf() {
    write_conf <<EOF
worker_processes 1;
error_log logs/error.log notice;
pid logs/nginx.pid;
events { worker_connections 64; }
http {
    access_log off;
    limit_req_zone \$binary_remote_addr zone=rl:1m rate=10r/s;
    server {
        listen 18080;
        location / { root html; add_header X-V $1; }
    }
}
EOF
}

base_conf 1
start_nginx || exit 1

base_conf 2
ctl_patch /1/control/dynamic > /dev/null
sleep 1
assert_eq "$(curl -sI "http://127.0.0.1:$PORT/" | grep -c 'X-V: 2')" "1" \
    "allowed change applied"

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
        location / { root html; add_header X-V 2; }
        location /n { return 200 "n\n"; }
    }
}
EOF
ctl_patch /1/control/dynamic > /dev/null
sleep 1
assert_eq "$(http_body /n)" "n" "allowed location addition applied"
assert_eq "$(grep -c 'dyn_conf: rejected' "$LOG" 2>/dev/null)" "0" \
    "location addition not rejected as resolver change"

write_conf <<'EOF'
worker_processes 1;
error_log logs/error.log notice;
pid logs/nginx.pid;
events { worker_connections 64; }
http {
    access_log off;
    limit_req_zone $binary_remote_addr zone=rl:1m rate=10r/s;
    limit_req_zone $binary_remote_addr zone=rl2:1m rate=10r/s;
    server {
        listen 18080;
        location / { root html; add_header X-V 3; }
    }
}
EOF
ctl_patch /1/control/dynamic > /dev/null
sleep 0.5
assert_log_contains "dyn_conf: rejected" "new shm zone rejected"
assert_eq "$(curl -sI "http://127.0.0.1:$PORT/" | grep -c 'X-V: 2')" "1" \
    "rejected change left old config serving"

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
        location / { root html; add_header X-V 4; }
    }
    server {
        listen 18081;
        location / { root html; }
    }
}
EOF
ctl_patch /1/control/dynamic > /dev/null
sleep 0.5
assert_eq "$(grep -c 'dyn_conf: rejected: listen' "$LOG" 2>/dev/null)" "1" \
    "new listen rejected"
assert_eq "$(curl -sI "http://127.0.0.1:$PORT/" | grep -c 'X-V: 2')" "1" \
    "listen rejection left old config serving"

write_conf <<'EOF'
worker_processes 1;
error_log logs/error.log notice;
pid logs/nginx.pid;
events { worker_connections 64; }
http {
    access_log off;
    server {
        listen 18080;
        location / { root html; add_header X-V 5; }
    }
}
EOF
ctl_patch /1/control/dynamic > /dev/null
sleep 0.5
assert_eq "$(grep -c 'dyn_conf: rejected: shared memory' "$LOG" 2>/dev/null)" "1" \
    "removed shm zone rejected"
assert_eq "$(curl -sI "http://127.0.0.1:$PORT/" | grep -c 'X-V: 2')" "1" \
    "shm removal left old config serving"

write_conf <<'EOF'
worker_processes 2;
error_log logs/error.log notice;
pid logs/nginx.pid;
events { worker_connections 64; }
http {
    access_log off;
    limit_req_zone $binary_remote_addr zone=rl:1m rate=10r/s;
    server {
        listen 18080;
        location / { root html; add_header X-V 6; }
    }
}
EOF
ctl_patch /1/control/dynamic > /dev/null
sleep 0.5
assert_eq "$(grep -c 'dyn_conf: rejected: worker_processes' "$LOG" 2>/dev/null)" "1" \
    "worker_processes change rejected"
assert_eq "$(curl -sI "http://127.0.0.1:$PORT/" | grep -c 'X-V: 2')" "1" \
    "worker_processes rejection left old config serving"

write_conf <<'EOF'
worker_processes 1;
error_log logs/error.log notice;
pid logs/nginx.pid;
events { worker_connections 64; }
http {
    access_log off;
    limit_req_zone $binary_remote_addr zone=rl:1m rate=10r/s;
    client_max_body_size 2m;
    server {
        listen 18080;
        location / { root html; add_header X-V 9; }
    }
}
EOF
ctl_patch /1/control/dynamic > /dev/null
sleep 0.5
assert_eq "$(grep -c 'dyn_conf: rejected: directive outside server/location' "$LOG" 2>/dev/null)" "1" \
    "http-level directive change rejected"
assert_eq "$(curl -sI "http://127.0.0.1:$PORT/" | grep -c 'X-V: 2')" "1" \
    "http-level rejection left old config serving"

write_conf <<'EOF'
worker_processes 1;
error_log logs/error2.log notice;
pid logs/nginx.pid;
events { worker_connections 64; }
http {
    access_log off;
    limit_req_zone $binary_remote_addr zone=rl:1m rate=10r/s;
    server {
        listen 18080;
        location / { root html; add_header X-V 10; }
    }
}
EOF
ctl_patch /1/control/dynamic > /dev/null
sleep 0.5
assert_eq "$(grep -c 'dyn_conf: rejected: directive outside server/location' "$LOG" 2>/dev/null)" "2" \
    "main error_log change rejected"
assert_eq "$(curl -sI "http://127.0.0.1:$PORT/" | grep -c 'X-V: 2')" "1" \
    "main error_log rejection left old config serving"

stop_nginx
make_prefix

write_conf <<'EOF'
worker_processes 1;
error_log logs/error.log notice;
pid logs/nginx.pid;
events { worker_connections 64; }
http {
    access_log off;
    resolver 127.0.0.1;
    server {
        listen 18080;
        location / { root html; add_header X-V 7; }
    }
}
EOF
start_nginx || exit 1

write_conf <<'EOF'
worker_processes 1;
error_log logs/error.log notice;
pid logs/nginx.pid;
events { worker_connections 64; }
http {
    access_log off;
    resolver 8.8.8.8;
    server {
        listen 18080;
        location / { root html; add_header X-V 8; }
    }
}
EOF
ctl_patch /1/control/dynamic > /dev/null
sleep 0.5
assert_eq "$(grep -c 'dyn_conf: rejected: resolver' "$LOG" 2>/dev/null)" "1" \
    "resolver change rejected"
assert_eq "$(curl -sI "http://127.0.0.1:$PORT/" | grep -c 'X-V: 7')" "1" \
    "resolver rejection left old config serving"

stop_nginx
report reject
