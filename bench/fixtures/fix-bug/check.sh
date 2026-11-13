#!/bin/sh
set -e
cc -std=c99 -O0 sum.c -o .sum_test
out=$(./.sum_test)
if [ "$out" != "6" ]; then echo "expected 6, got $out"; exit 1; fi
echo ok
