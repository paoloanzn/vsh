#!/bin/sh
# usage: ./compile.sh [check|debug]
CC=clang
FLAGS="-std=gnu17"
BUILD_DIR=build
DEBUG_FLAGS="-g -O0 -DDEBUG"
SANITIZE_FLAGS="-fsanitize=address -g -fno-omit-frame-pointer"
CMD="$1"

mkdir -p "$BUILD_DIR"

if [ "$CMD" = "check" ]
then
    $CC $FLAGS $SANITIZE_FLAGS shell.c -o "$BUILD_DIR/shell" && "$BUILD_DIR/shell"
elif [ "$CMD" = "debug" ]
then
    $CC $FLAGS $DEBUG_FLAGS shell.c -o "$BUILD_DIR/shell"
else
    $CC $FLAGS shell.c -o "$BUILD_DIR/shell"
fi