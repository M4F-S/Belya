#!/bin/sh
set -e
out=$(python3 stats.py)
[ "$out" = "4" ] || { echo "expected 4, got $out"; exit 1; }
echo ok
