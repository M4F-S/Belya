#!/bin/sh
if [ -n "$1" ]; then
    echo -n "$1" | sha256sum | awk '{print $1}'
else
    echo "Error: no input provided" >&2
    exit 1
fi
