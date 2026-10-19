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

n=0
while [ "$(http_code /)" != "200" ] && [ "$n" -lt 50 ]; do
    sleep 0.1
    n=$((n + 1))
done

READY="$TMP_ROOT/ka_ready"
GO="$TMP_ROOT/ka_go"
BODY="$TMP_ROOT/ka_body"
rm -f "$READY" "$GO" "$BODY"

bash -c '
    port=$1
    ready=$2
    go=$3
    out=$4

    trap "" PIPE

    exec 3<>/dev/tcp/127.0.0.1/$port || exit 1

    printf "GET / HTTP/1.1\r\nHost: a\r\nConnection: keep-alive\r\n\r\n" >&3 || exit 1
    sleep 1
    timeout 1 cat <&3 > "$out"
    : > "$ready"

    n=0
    while [ ! -f "$go" ] && [ "$n" -lt 50 ]; do
        sleep 0.1
        n=$((n + 1))
    done

    printf "GET / HTTP/1.1\r\nHost: a\r\nConnection: keep-alive\r\n\r\n" >&3 || true
    sleep 1
    timeout 1 cat <&3 >> "$out"

    printf "GET / HTTP/1.1\r\nHost: a\r\nConnection: keep-alive\r\n\r\n" >&3 || true
    sleep 1
    timeout 1 cat <&3 >> "$out"
' _ "$PORT" "$READY" "$GO" "$BODY" >/dev/null 2>&1 &
hold=$!

n=0
while [ ! -f "$READY" ] && [ "$n" -lt 50 ]; do
    sleep 0.1
    n=$((n + 1))
done

write_conf <<'EOF'
worker_processes 1;
error_log logs/error.log notice;
pid logs/nginx.pid;
events { worker_connections 64; }
http {
    access_log off;
    server {
        listen 18080;
        location / { root html; add_header X-Marker v2; }
    }
}
EOF

ctl_patch /1/control/dynamic > /dev/null
sleep 1

n=0
while ! grep -q 'dyn_conf: worker swapped to gen=1' "$LOG" 2>/dev/null \
      && [ "$n" -lt 100 ]; do
    sleep 0.1
    n=$((n + 1))
done

: > "$GO"
wait $hold 2>/dev/null

assert_log_contains "dyn_conf: worker swapped to gen=1" "generation applied while keepalive held"

count=$(grep -c 'HTTP/1.1 200' "$BODY" 2>/dev/null)
assert_eq "$count" "2" "stale-generation keepalive connection closed, third request unanswered"

rm -f "$READY" "$GO" "$BODY"

report keepalive_gen
