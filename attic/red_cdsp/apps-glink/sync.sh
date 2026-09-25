#!/bin/bash
# Copy the in-tree sources next to the Makefile, adapting them to the 6.8 host
# kernel (platform_driver.remove still returns int there), and build on red.
set -e
L=${LINUX:-$HOME/src/linux}
cd "$(dirname "$0")"
cp $L/drivers/net/rpmsg_net.c $L/drivers/rpmsg/qcom_glink_native.h .
sed -e 's/^static void qcom_glink_shmem_remove(/static int qcom_glink_shmem_remove(/' \
    -e '/qcom_glink_native_remove(shmem->glink);/a\	return 0;' \
    $L/drivers/rpmsg/qcom_glink_shmem.c > qcom_glink_shmem.c
ssh red 'rm -rf ~/apps-glink && mkdir -p ~/apps-glink'
scp -q Makefile *.c *.h red:apps-glink/
ssh red 'cd ~/apps-glink && make -C /usr/src/linux-headers-$(uname -r) M=$PWD modules 2>&1 | grep -v "^  *The\|^warning" | tail -15; ls -la *.ko'
# Install the modules and the service that brings the link up at boot (needs the modules built above):
#   ./sync.sh install
if [ "${1:-}" = install ]; then
	scp -q cdsp-net-up.sh cdsp-net.service red:
	ssh red 'sudo mkdir -p /opt/cdsp && cp ~/apps-glink/qcom_glink_shmem.ko ~/apps-glink/rpmsg_net.ko /tmp/ && sudo cp /tmp/qcom_glink_shmem.ko /tmp/rpmsg_net.ko ~/cdsp-net-up.sh /opt/cdsp/ && sudo cp ~/cdsp-net.service /etc/systemd/system/ && sudo systemctl daemon-reload && sudo systemctl enable cdsp-net.service'
fi
