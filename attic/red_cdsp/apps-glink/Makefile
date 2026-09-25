# Out-of-tree build of the GLINK shared-memory transport and rpmsg_net for the
# RubikPi 3's stock 6.8 kernel, from the same sources as the in-tree drivers:
#   make -C /usr/src/linux-headers-$(uname -r) M=$PWD modules
obj-m += qcom_glink_shmem.o rpmsg_net.o
ccflags-y += -I$(src) -DGLINK_SHMEM_RUBIKPI_HOST
