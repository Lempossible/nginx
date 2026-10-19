#!/bin/sh
. "$(dirname "$0")/harness.sh"

build_nginx || { echo "build failed"; exit 1; }

cd "$NGX_SRC" || exit 1

assert_eq "$(grep -c 'ngx_dyn_conf' auto/sources)" "2" \
    "new core source is registered in auto/sources"

out=$(./configure --with-control-api 2>&1)
assert_eq "$?" "0" "configure succeeds with new core files"

out=$(make -j"$(nproc)" 2>&1)
assert_eq "$?" "0" "make succeeds with new core files"

report pool_stats
