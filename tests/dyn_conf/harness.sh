#!/bin/sh

NGX_SRC="$(cd "$(dirname "$0")/../.." && pwd)"
NGX_BIN="$NGX_SRC/objs/nginx"
TMP_ROOT="${TMP_ROOT:-/tmp/ngx-dyn-test}"
PREFIX="$TMP_ROOT/prefix"
CTL_SOCK="$TMP_ROOT/control.sock"
LOG="$PREFIX/logs/error.log"
PORT=18080
FAILS=0
CHECKS=0

build_nginx() {
    cd "$NGX_SRC" || return 1
    if [ -x ./configure ]; then
        ./configure --with-control-api >/dev/null 2>&1 || return 1
    else
        ./auto/configure --with-control-api >/dev/null 2>&1 || return 1
    fi
    make -j"$(nproc)" >/dev/null 2>&1 || return 1
}

stop_nginx() {
    if [ -f "$PREFIX/logs/nginx.pid" ]; then
        "$NGX_BIN" -p "$PREFIX" -c "$PREFIX/conf/nginx.conf" -s quit >/dev/null 2>&1
        for _ in 1 2 3 4 5 6 7 8 9 10; do
            [ -f "$PREFIX/logs/nginx.pid" ] || break
            sleep 0.2
        done
    fi
    rm -f "$CTL_SOCK"
    return 0
}

make_prefix() {
    stop_nginx
    rm -rf "$TMP_ROOT"
    mkdir -p "$PREFIX/logs" "$PREFIX/html" "$PREFIX/conf"
    printf 'static-root\n' > "$PREFIX/html/index.html"
    rm -f "$CTL_SOCK"
}

write_conf() {
    cat > "$PREFIX/conf/nginx.conf"
}

write_dyn_fragment() {
    mkdir -p "$PREFIX/conf/dyn"
    cat > "$PREFIX/conf/dyn/http.conf"
}

start_nginx() {
    "$NGX_BIN" -p "$PREFIX" -c "$PREFIX/conf/nginx.conf" -l "$CTL_SOCK" >/dev/null 2>&1
    for _ in $(seq 1 60); do
        [ -S "$CTL_SOCK" ] && return 0
        sleep 0.1
    done
    echo "--- nginx did not start; $LOG ---"
    cat "$LOG" 2>/dev/null
    return 1
}

ctl() {
    curl -s --unix-socket "$CTL_SOCK" -o /dev/stdout -w '\n%{http_code}\n' \
        "http://localhost$1"
}

ctl_patch() {
    curl -s -X PATCH --unix-socket "$CTL_SOCK" -o /dev/stdout -w '\n%{http_code}\n' \
        "http://localhost$1"
}

http_body() {
    curl -s "http://127.0.0.1:$PORT$1"
}

http_code() {
    curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT$1"
}

assert_eq() {
    CHECKS=$((CHECKS + 1))
    if [ "$1" = "$2" ]; then
        echo "ok   - $3"
    else
        FAILS=$((FAILS + 1))
        echo "FAIL - $3"
        echo "       expected: [$2]"
        echo "       actual:   [$1]"
    fi
}

assert_contains() {
    CHECKS=$((CHECKS + 1))
    if printf '%s' "$1" | grep -qF -- "$2"; then
        echo "ok   - $3"
    else
        FAILS=$((FAILS + 1))
        echo "FAIL - $3"
        echo "       expected to contain: [$2]"
        echo "       actual:              [$1]"
    fi
}

assert_log_contains() {
    CHECKS=$((CHECKS + 1))
    if grep -qF -- "$1" "$LOG" 2>/dev/null; then
        echo "ok   - $2"
    else
        FAILS=$((FAILS + 1))
        echo "FAIL - $2 (not found in $LOG: [$1])"
        tail -n 30 "$LOG" 2>/dev/null
    fi
}

rss_kb() {
    pid=$(cat "$PREFIX/logs/nginx.pid" 2>/dev/null)
    [ -n "$pid" ] || { echo 0; return; }
    ps -o rss= -p "$pid" 2>/dev/null | tr -d ' '
}

report() {
    echo
    echo "$1: $CHECKS checks, $FAILS failures"
    [ "$FAILS" -eq 0 ]
}
