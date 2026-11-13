#!/bin/sh
set -e
cc -std=c99 -c a.c -o a.o
cc -std=c99 -c b.c -o b.o
cc -std=c99 -c c.c -o c.o
n=$(grep -h "define TIMEOUT_MS" *.h *.c 2>/dev/null | wc -l)
if [ "$n" -ne 1 ]; then echo "TIMEOUT_MS defined $n times"; exit 1; fi
if ! grep -q "TIMEOUT_MS" config.h 2>/dev/null; then echo "config.h missing TIMEOUT_MS"; exit 1; fi
echo ok
