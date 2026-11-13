#!/bin/sh
set -e
cc -std=c99 -O0 main.c helper.c -o .app
out=$(./.app)
[ "$out" = "42" ] || { echo "expected 42, got $out"; exit 1; }
echo ok
