#!/bin/bash

RANDOMID=$((1000 + RANDOM % 9000))
BUILD_DIR="build$RANDOMID"

mkdir -p "$BUILD_DIR/bin" "$BUILD_DIR/lib"

CGO="./crt0/crt0.ssk"

if [ ! -f "$CGO" ]; then
    echo "Error: $CGO not found."
    exit 1
fi

chmod +x "$CGO"

export PREFIX="$BUILD_DIR"

$CGO -w -shared -o libfrees.so native/frees/src/*.c -Inative/frees/include
$CGO -w -o glog native/glog/src/glog.c
$CGO -w -o gp native/gp/src/gp.c
$CGO -w -o mgr native/mgr/src/mgr.c native/mgr/src/sdk.c -Inative/mgr/include -lcurl
$CGO -w -o csd native/csd/src/*.c -Inative/csd/include
$CGO -w -o kdio native/kdio/src/kdio.c

mv libfrees.so "$BUILD_DIR/lib/"
mv glog gp mgr csd kdio "$BUILD_DIR/bin/"

cp "$CGO" "$BUILD_DIR/bin/cgo"
