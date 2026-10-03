CC=clang
FLAGS=
BUILD_DIR=build

mkdir -p $BUILD_DIR

$CC $FLAGS shell.c -o $BUILD_DIR/shell