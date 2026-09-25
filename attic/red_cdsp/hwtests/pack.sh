#!/bin/bash
# Pack rootfs-min plus the built tests and the runner into an initramfs: pack.sh <out.cpio>
set -eu
OUT=${OUT:-/tmp/hwtests}
H=$(cd "$(dirname "$0")" && pwd)
N=$H/../newkernel
G=${GEN_INIT_CPIO:-$HOME/src/linux/obj_qcs6490_cdsp/usr/gen_init_cpio}
L=$OUT/test.list
cp $N/rootfs-min.list $L
{
	echo "dir /tests 755 0 0"
	echo "dir /tests/hvx 755 0 0"
	echo "dir /tests/hmx 755 0 0"
	echo "file /bin/runtests $H/runtests.sh 755 0 0"
	for t in $OUT/hvx/* $OUT/hmx/*; do
		case $t in *.log) continue;; esac
		echo "file /tests/${t#$OUT/} $t 755 0 0"
	done
} >> $L
$G $L > $1
ls -la $1
