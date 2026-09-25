#!/bin/bash
# usage: build.sh <name> <cflags...>   -> $S/db/out/<name>/{dropbear,dropbearkey}
set -e
S=/tmp/claude-1000/-home-brian-src-qemu/447866c9-a019-4c06-bd73-d713a270e957/scratchpad
export PATH=/opt/clang+llvm-22.1.8-cross-hexagon-unknown-linux-musl/x86_64-ubuntu-22.04/bin:$PATH
name=$1; export HEXOPT=$2; export HEXEXTRA="$3"; shift 3
rm -rf $S/db/w_$name && cp -r $S/db/src $S/db/w_$name && cd $S/db/w_$name
make clean >/dev/null 2>&1 || true
CFLAGS="-fno-strict-overflow -Wno-pointer-sign -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64" \
LDFLAGS="-fuse-ld=lld -static" \
make -j26 PROGRAMS="dropbear dropbearkey" STATIC=1 \
  CC=/tmp/claude-1000/-home-brian-src-qemu/447866c9-a019-4c06-bd73-d713a270e957/scratchpad/db/hexcc.sh AR=llvm-ar RANLIB=llvm-ranlib >build.log 2>&1 || { tail -15 build.log; exit 1; }
mkdir -p $S/db/out/$name && cp dropbear dropbearkey $S/db/out/$name/
ls -la $S/db/out/$name/
