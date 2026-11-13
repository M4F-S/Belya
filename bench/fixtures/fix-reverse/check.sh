#!/bin/sh
set -e
cc -std=c99 -O0 reverse.c -o .rev_test
out=$(./.rev_test)
[ "$out" = "cba" ] || { echo "expected cba, got $out"; exit 1; }
echo ok
