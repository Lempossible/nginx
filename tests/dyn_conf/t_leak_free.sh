#!/bin/sh
. "$(dirname "$0")/harness.sh"

build_nginx || { echo "build failed"; exit 1; }

make_prefix

gen_conf() {
    n=$1
    write_conf <<EOF
worker_processes 1;
error_log logs/error.log notice;
pid logs/nginx.pid;
events { worker_connections 64; }
http {
    access_log off;
    server {
        listen 18080;
        keepalive_timeout 2s;
        client_max_body_size ${n}m;
        location / { root html; add_header X-N $n; }
    }
}
EOF
}

worker_rss_kb() {
    mpid=$(cat "$PREFIX/logs/nginx.pid" 2>/dev/null)
    [ -n "$mpid" ] || { echo 0; return; }
    for wp in $(ps --ppid "$mpid" -o pid= 2>/dev/null); do
        ps -o rss= -p "$wp" 2>/dev/null | tr -d ' '
    done | awk '{s += $1} END { print s + 0 }'
}

gen_conf 1
start_nginx || exit 1
sleep 1
before=$(worker_rss_kb)

i=2
while [ "$i" -le 50 ]; do
    gen_conf "$i"
    ctl_patch /1/control/dynamic > /dev/null
    curl -s -o /dev/null "http://127.0.0.1:$PORT/"
    i=$((i + 1))
done

sleep 3
after=$(worker_rss_kb)

echo "worker rss before=${before}kb after=${after}kb"

assert_eq "$(grep -c 'dyn_conf: swap rejected' "$LOG" 2>/dev/null)" "0" \
    "no rejected swaps across 49 hot reloads"
assert_eq "$(grep -c 'dyn_conf: worker swapped' "$LOG")" "49" \
    "all 49 generations applied"
assert_eq "$(grep -c 'dyn_conf: gen=.* freed blocks=' "$LOG")" "98" \
    "98 pools freed: 49 master probes + 49 superseded generations"
assert_eq "$(grep -c 'dyn_conf: gen=0 freed blocks=' "$LOG")" "1" \
    "boot generation freed exactly once"

delta=$((after - before))
if [ "$delta" -lt 0 ]; then delta=$((-delta)); fi

CHECKS=$((CHECKS + 1))
if [ "$delta" -le 2048 ]; then
    echo "ok   - worker rss delta ${delta}kb within 2mb"
else
    FAILS=$((FAILS + 1))
    echo "FAIL - worker rss grew ${delta}kb (>2mb) across 49 generations"
fi

report leak_free
