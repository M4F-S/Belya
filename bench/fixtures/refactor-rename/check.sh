#!/bin/sh
set -e
if grep -q "calc_total" *.c *.h 2>/dev/null; then echo "old symbol still present"; exit 1; fi
cc -std=c99 -O0 main.c calc.c -o .app
out=$(./.app)
[ "$out" = "42" ] || { echo "expected 42, got $out"; exit 1; }
echo ok
