#!/bin/bash
# Build the CDSP firmware (H2 + kernel + DTB + initramfs in one PIL image) and,
# with "install", put it on red as the CDSP's firmware and reboot the board: the
# APPS remoteproc then loads and starts it at boot, nothing else is needed.
#   mkfw.sh <initramfs.cpio> [install]
set -euo pipefail
L=${LINUX:-$HOME/src/linux}
O=$L/obj_qcs6490_cdsp
Q=$HOME/src/qemu
OUT=${OUT:-/tmp}
CPIO=$1
# The DSP core runs at the 19.2 MHz reference clock, so unpacking a compressed initramfs
# takes far longer than copying an uncompressed one; keep the rootfs small (rootfs-min.list)
# and leave it uncompressed.  COMPRESS=zstd is there for a rootfs that will not fit.
IMG=$CPIO
if [ "${COMPRESS:-}" = zstd ]; then
	zstd -19 -q -f $CPIO -o $OUT/initramfs.cpio.zst
	IMG=$OUT/initramfs.cpio.zst
fi
# The shared-memory windows of the GLINK link outlive a reboot; clear them before the DSP starts
python3 $L/scripts/hexagon/mkheximg -o $OUT/cdsp_fw.elf --region-end 0x8a700000 --zero 0xd7c00000:0x20000 \
	--h2 ${H2:-$Q/pc-bios/hexagon_loadlinux_qcs6490_cdsp} --linux $O/vmlinux \
	--dtb $O/arch/hexagon/boot/dts/qcs6490-cdsp.dtb --linux-addr 0xa1000000 ${DTBARGS:-} \
	--initramfs $IMG --initramfs-addr 0xa8000000 | tail -3
python3 $Q/attic/red_cdsp/h2patch/qtestsign/qtestsign.py -v6 cdsp -o $OUT/cdsp.mbn $OUT/cdsp_fw.elf > /dev/null
ls -la $OUT/cdsp.mbn
if [ "${2:-}" = install ]; then
	scp -q $OUT/cdsp.mbn red:cdsp_new.mbn
	ssh red 'sudo cp ~/cdsp_new.mbn /usr/lib/firmware/updates/qcom/qcs6490/cdsp.mbn && sync && sudo reboot' || true
fi
# Restore the board's original firmware:
#   sudo cp /usr/lib/firmware/updates/qcom/qcs6490/original-cdsp.mbn /usr/lib/firmware/updates/qcom/qcs6490/cdsp.mbn
# (Qualcomm's own image; ~/cdsp.mbn.custom-orig-20260925 is the idle-H2 image of the earlier procedure.)
