#!/bin/sh
# usage: ./get-port.sh [proc_name]
PROC_NAME="$1"

if [ -z "$PROC_NAME" ]; then
    echo "usage: get-port.sh [proc_name]"
    exit 1
fi

PID=$(ps a | grep -w "$PROC_NAME" | head -1 | awk -F'[^0-9]*' '$0=$2')
PORT=$((10000 + $PID % 55536))

echo "$PORT"