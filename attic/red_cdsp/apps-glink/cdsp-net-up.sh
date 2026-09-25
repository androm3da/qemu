#!/bin/sh
# Bring up the IP link to the Linux running on the CDSP, once the CDSP's remoteproc has
# started it: the APPS side of the GLINK edge (qcom_glink_shmem) and rpmsg_net, then dsp0.
# The DSP creates the shared-memory windows and opens the IP_BRIDGE channel itself, so the
# module is retried until the DSP has got that far (its probe fails while the windows are
# not initialized, before it touches any doorbell).
D=${CDSP_DIR:-/opt/cdsp}
APPS_IP=${APPS_IP:-10.42.0.1}
DSP_IP=${DSP_IP:-10.42.0.2}
log() { echo "cdsp-net: $*"; }

# The CDSP's remoteproc; its number changes from boot to boot.
rp=
for i in $(seq 1 60); do
	for d in /sys/class/remoteproc/remoteproc*; do
		[ "$(cat $d/name 2>/dev/null)" = a300000.remoteproc ] && rp=$d
	done
	[ -n "$rp" ] && [ "$(cat $rp/state)" = running ] && break
	sleep 2
done
[ -n "$rp" ] && [ "$(cat $rp/state)" = running ] || { log "the CDSP is not running"; exit 1; }
log "the CDSP is running ($rp)"

lsmod | grep -q '^rpmsg_net ' || insmod $D/rpmsg_net.ko || exit 1

bound=/sys/bus/platform/drivers/qcom_glink_shmem/d7c10000.snull_net
for i in $(seq 1 120); do
	ip link show dsp0 >/dev/null 2>&1 && break
	if [ ! -e $bound ]; then
		# not loaded, or loaded but the DSP was not ready for it
		lsmod | grep -q '^qcom_glink_shmem ' && rmmod qcom_glink_shmem
		insmod $D/qcom_glink_shmem.ko intentless=1 2>/dev/null
	fi
	sleep 2
done
ip link show dsp0 >/dev/null 2>&1 || { log "dsp0 did not appear"; exit 1; }

ip addr replace $APPS_IP peer $DSP_IP dev dsp0
ip link set dsp0 up
log "dsp0 is up: $APPS_IP <-> $DSP_IP"
