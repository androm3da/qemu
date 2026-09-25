#!/bin/bash
# usage: deploy_dsp.sh <initramfs.cpio> [vmlinux.bin]   -- reboot the board and boot the DSP kernel
set -u
S=/tmp/claude-1000/-home-brian-src-qemu/447866c9-a019-4c06-bd73-d713a270e957/scratchpad
CPIO=$1; BIN=${2:-$S/board/vmlinux.bin}
cp ~/src/linux/obj_qcs6490_cdsp/arch/hexagon/boot/dts/qcs6490-cdsp.dtb $S/board/cdsp.dtb
SZ=$(stat -c %s $CPIO)
fdtput -t x $S/board/cdsp.dtb /chosen linux,initrd-start 0 0xa8000000
fdtput -t x $S/board/cdsp.dtb /chosen linux,initrd-end 0 $(printf '0x%x' $((0xa8000000+SZ)))
scp -q $BIN red:newk/vmlinux.bin && scp -q $S/board/cdsp.dtb red:newk/cdsp.dtb && scp -q $CPIO red:newk/cdsp-init.cpio || exit 1
ssh red 'sudo reboot' ; sleep 20
for i in $(seq 1 40); do ssh -o ConnectTimeout=4 -o BatchMode=yes red 'test $(cut -d. -f1 /proc/uptime) -lt 120' 2>/dev/null && break; sleep 5; done
sleep 30
ssh -o ConnectTimeout=10 red 'cd ~/newk && timeout 100 ./boot.sh 2>&1 | tail -2'
ssh red 'for i in $(seq 1 40); do if sudo dd if=/dev/mem bs=4096 skip=$((0xa1791000/4096)) count=80 2>/dev/null | strings | grep -a -q "cdsp-init: alive 1"; then echo "dsp userspace up"; break; fi; sleep 3; done'
