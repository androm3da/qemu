#!/bin/sh
# Boot the kernel in ~/newk on the CDSP.  h2 must be freshly started and
# waiting for its start signal; pass "restart" to restart the CDSP first.
# All loads are uncached (see memwrite.py): the CDSP is not coherent with the
# APPS caches.
cd ~/newk
for d in /sys/class/remoteproc/remoteproc*; do
	[ "$(readlink -f $d/device)" = "/sys/devices/platform/soc@0/a300000.remoteproc" ] && RP=$d
done
echo "cdsp remoteproc: $RP ($(cat $RP/state))"
if [ "$1" = restart ]; then
	echo stop  | sudo tee $RP/state >/dev/null; sleep 2
	echo start | sudo tee $RP/state >/dev/null; sleep 8
	echo "restarted: $(cat $RP/state)"
fi
sudo ./memwrite.py 0xa1000000 zero:0xf000000
sudo ./memwrite.py 0xa1000000 vmlinux.bin
sudo ./memwrite.py 0xa8000000 cdsp-init.cpio
sudo ./memwrite.py 0xa1000000 check:vmlinux.bin
sudo ./memwrite.py 0xa1001200 cdsp.dtb
sudo ./memwrite.py 0xa1001200 check:cdsp.dtb
sudo ./memwrite.py 0xa8000000 check:cdsp-init.cpio
echo "poke"; sudo busybox devmem 0x17C0000C 32 0x20
