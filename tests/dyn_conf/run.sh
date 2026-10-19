#!/bin/sh

cd "$(dirname "$0")" || exit 1
rc=0
for t in t_*.sh; do
    echo "=== $t ==="
    sh "$t" || rc=1
done
exit $rc
