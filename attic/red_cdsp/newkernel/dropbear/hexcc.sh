#!/bin/bash
# compiler wrapper: force one optimization level (HEXOPT, default 2) plus HEXEXTRA on every compile
args=()
for a in "$@"; do case "$a" in -O0|-O1|-O2|-O3|-Os|-Oz) ;; *) args+=("$a");; esac; done
exec /opt/clang+llvm-22.1.8-cross-hexagon-unknown-linux-musl/x86_64-ubuntu-22.04/bin/hexagon-unknown-linux-musl-clang "${args[@]}" -O${HEXOPT:-2} $HEXEXTRA
