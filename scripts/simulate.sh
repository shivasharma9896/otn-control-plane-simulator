#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
"$ROOT/bin/otn_node" -i 1001 -p 19101 -d 19100 <<EOF &
status
EOF
a=$!
"$ROOT/bin/otn_node" -i 1002 -p 19102 -d 19100 <<EOF &
status
EOF
b=$!
sleep 3
kill "$a" "$b" 2>/dev/null || true
wait "$a" 2>/dev/null || true
wait "$b" 2>/dev/null || true
echo "simulation complete"
