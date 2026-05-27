#!/bin/bash

RANDOMID=$((1000 + RANDOM % 9000))
BUILD_DIR="build$RANDOMID"

mkdir -p "$BUILD_DIR/bin" "$BUILD_DIR/lib"

CGO_SRC="native/cgo/src/cgo.c"
CGO_BIN="./cgo"

gcc -Os -s -fno-asynchronous-unwind-tables -o "$CGO_BIN" "$CGO_SRC"
chmod +x "$CGO_BIN"

export PREFIX="$BUILD_DIR"

"$CGO_BIN" -w -shared -o libfrees.so native/frees/src/*.c -Inative/frees/include
"$CGO_BIN" -w -o glog native/glog/src/glog.c
"$CGO_BIN" -w -o gp native/gp/src/gp.c
"$CGO_BIN" -w -o mgr native/mgr/src/mgr.c native/mgr/src/sdk.c -Inative/mgr/include -lcurl
"$CGO_BIN" -w -o csd native/csd/src/*.c -Inative/csd/include
"$CGO_BIN" -w -o kdio native/kdio/src/kdio.c

mv libfrees.so "$BUILD_DIR/lib/"
mv glog gp mgr csd kdio "$BUILD_DIR/bin/"

rm "$CGO_BIN"

mv ./crt0/crt0.ssk "$BUILD_DIR/bin/cgo"
chmod +x "$BUILD_DIR/bin/cgo"
