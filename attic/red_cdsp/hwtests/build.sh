#!/bin/bash
# Build the HVX and HMX tests for the CDSP's Linux: the HMX tests are the ones of QEMU's
# bcain/hmx branch (tests/tcg/hexagon/hmx_*.c), the HVX ones are QEMU's own plus hvx_ctx.c.
# Output: $OUT/{hvx,hmx}/<test>
set -eu
Q=${QEMU:-$HOME/src/qemu}
OUT=${OUT:-/tmp/hwtests}
H=$(cd "$(dirname "$0")" && pwd)
export PATH=/opt/clang+llvm-22.1.8-cross-hexagon-unknown-linux-musl/x86_64-ubuntu-22.04/bin:$PATH
CC="hexagon-unknown-linux-musl-clang -O1 -static -Wno-incompatible-pointer-types -Wno-undefined-internal -Wno-unused-function -Wno-gnu-folding-constant"
T=$Q/tests/tcg/hexagon
mkdir -p $OUT/src $OUT/hvx $OUT/hmx
for f in $(git -C $Q ls-tree --name-only bcain/hmx tests/tcg/hexagon/ | grep 'hmx_.*\.c$'); do
	git -C $Q show bcain/hmx:$f > $OUT/src/$(basename $f)
done
cp $T/hex_test.h $T/hvx_misc.h $T/hvx_histogram_input.h $T/hvx_histogram_row.h $T/hvx_histogram_row.S $OUT/src/
ok=0; bad=0
build() { # name flags... -- sources
	n=$1; shift
	if $CC "$@" -o $OUT/$n 2> $OUT/$n.log; then ok=$((ok+1)); else bad=$((bad+1)); echo "BUILD FAILED: $n"; fi
}
for s in $OUT/src/hmx_*.c; do
	n=$(basename $s .c)
	# put the test's buffers in the section that the shim maps to the VTCM
	sed -E 's/^(static [a-z0-9_]+ +[a-z0-9_]+\[[^]]+\]) +__attribute__\(\(aligned\(([0-9]+)\)\)\);/\1 __attribute__((aligned(\2), section(".vtcm")));/' $s > $OUT/src/vtcm_$n.c
	build hmx/$n -mv81 -mhvx -include $H/vtcm_shim.h -I$OUT/src -Wl,--section-start=.vtcm=0x50000000 $OUT/src/vtcm_$n.c
done
build hvx/hvx_ctx -mv68 -mhvx -mhvx-length=128B $H/hvx_ctx.c
build hvx/hvx1 -mv68 -mhvx -mhvx-length=128B $H/hvx1.c
build hvx/hvx_histogram -mv68 -mhvx -I$OUT/src $T/hvx_histogram.c $OUT/src/hvx_histogram_row.S
build hvx/v68_hvx -mv68 -mhvx -I$OUT/src $T/v68_hvx.c
build hvx/vector_add_int -mv68 -mhvx -fvectorize -I$OUT/src $T/vector_add_int.c
echo "built $ok, failed $bad"
