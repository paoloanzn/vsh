#!/bin/sh
# usage: ./get-port.sh [proc_name]
# Prints the port of the spy server of the running process named `proc_name`
# (e.g. shell). Uses the same formula as start_server() in spy.c.
PROC_NAME="$1"

if [ -z "$PROC_NAME" ]; then
    echo "usage: get-port.sh [proc_name]"
    exit 1
fi

# PID of the first matching process: the first number on its `ps` line.
PID=$(ps a | grep -w "$PROC_NAME" | head -1 | awk -F'[^0-9]*' '$0=$2')
PORT=$((10000 + $PID % 55536))

echo "$PORT"