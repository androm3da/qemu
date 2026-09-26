#!/bin/bash
# Pack rootfs-min plus perf, the PMU workload and scripts into an initramfs: pack.sh <out.cpio>
set -eu
H=$(cd "$(dirname "$0")" && pwd)
PERF=${PERF:-/tmp/perf_hex/perf}
G=${GEN_INIT_CPIO:-$HOME/src/linux/obj_qcs6490_cdsp/usr/gen_init_cpio}
L=${TMPDIR:-/tmp}/perf.list
cp $H/../newkernel/rootfs-min.list $L
{
	echo "file /bin/perf $PERF 755 0 0"
	echo "file /bin/pmuwork $H/pmuwork 755 0 0"
	echo "file /bin/pmusweep $H/pmusweep.sh 755 0 0"
	echo "file /bin/perf-demo $H/perf-demo.sh 755 0 0"
} >> $L
$G $L > $1
ls -la $1
