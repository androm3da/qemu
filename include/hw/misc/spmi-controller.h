/*
 * Qualcomm SPMI (System Power Management Interface) Controller
 *
 * Copyright (c) 2025 Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This implements the SPMI controller found on Qualcomm SoCs like QCS6490.
 * SPMI is the interface protocol used to communicate with PMICs.
 *
 * Based on real hardware analysis and Linux kernel drivers:
 * - drivers/spmi/spmi-pmic-arb.c
 * - Documentation/devicetree/bindings/spmi/qcom,spmi-pmic-arb.yaml
 */

#ifndef HW_MISC_SPMI_CONTROLLER_H
#define HW_MISC_SPMI_CONTROLLER_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_SPMI_CONTROLLER "spmi-controller"
OBJECT_DECLARE_SIMPLE_TYPE(SPMIControllerState, SPMI_CONTROLLER)

/* SPMI Controller register space size */
#define SPMI_CONTROLLER_SIZE 0x10000  /* 64KB */

/* SPMI Protocol Constants */
#define SPMI_MAX_SLAVES     16  /* Maximum SPMI slaves per controller */
#define SPMI_MAX_PPID       4096  /* Maximum Peripheral PIDs */

/* SPMI Command Types */
#define SPMI_CMD_EXT_WRITE      0x00
#define SPMI_CMD_RESET          0x10
#define SPMI_CMD_SLEEP          0x11
#define SPMI_CMD_SHUTDOWN       0x12
#define SPMI_CMD_WAKEUP         0x13
#define SPMI_CMD_AUTHENTICATE   0x14
#define SPMI_CMD_MSTR_READ      0x15
#define SPMI_CMD_MSTR_WRITE     0x16
#define SPMI_CMD_TRANSFER_BUS_OWNERSHIP 0x1A
#define SPMI_CMD_DDB_MASTER_READ        0x1B
#define SPMI_CMD_DDB_SLAVE_READ         0x1C
#define SPMI_CMD_EXT_READ       0x20
#define SPMI_CMD_EXT_WRITEL     0x30
#define SPMI_CMD_EXT_READL      0x38
#define SPMI_CMD_ZERO_WRITE     0x80

/* SPMI Register Offsets (Qualcomm PMIC Arbiter) */
#define SPMI_CHANNEL_STATUS     0x0000
#define SPMI_CHANNEL_CMD        0x0004
#define SPMI_CHANNEL_CONFIG     0x0008
#define SPMI_CHANNEL_IRQ_CLEAR  0x0010

/* SPMI Observation Channel Registers */
#define SPMI_OBS_0_CMD          0x0040
#define SPMI_OBS_0_STATUS       0x0044
#define SPMI_OBS_0_CFG          0x0048

/* SPMI Configuration Registers */
#define SPMI_ARB_APID_MAP_OFFSET 0x0900  /* APID to PPID mapping */
#define SPMI_ARB_REG_CHN_OFFSET  0x8000  /* Register channel offset */

/* SPMI Status bits */
#define SPMI_STATUS_DONE        0x01
#define SPMI_STATUS_FAILURE     0x02
#define SPMI_STATUS_DENIED      0x04
#define SPMI_STATUS_DROPPED     0x08

/* SPMI Slave Device Structure */
typedef struct SPMISlave {
    uint8_t slave_id;    /* SPMI Slave ID (0-15) */
    DeviceState *device; /* Connected PMIC device */
    bool present;        /* Is slave present */
    char name[32];       /* Slave device name */
    uint64_t (*read)(void *opaque, uint16_t addr, unsigned size);
    void (*write)(void *opaque, uint16_t addr, uint64_t value, unsigned size);
    void *opaque;        /* Opaque pointer for callbacks */
} SPMISlave;

/* SPMI Channel State */
typedef struct SPMIChannel {
    uint32_t cmd;        /* Command register */
    uint32_t status;     /* Status register */
    uint32_t config;     /* Configuration register */
    bool active;         /* Channel active */
    uint8_t data[16];    /* Data buffer */
    uint8_t data_len;    /* Data length */
} SPMIChannel;

struct SPMIControllerState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    /* SPMI Bus Configuration */
    SPMISlave slaves[SPMI_MAX_SLAVES];
    uint8_t num_slaves;

    /* SPMI Channels (16 read + 16 write channels) */
    SPMIChannel channels[32];

    /* SPMI Observation Channels */
    SPMIChannel obs_channels[8];

    /* SPMI Arbiter Configuration */
    uint32_t apid_map[SPMI_MAX_PPID];  /* APID to PPID mapping */
    uint32_t ppid_to_apid[SPMI_MAX_PPID]; /* Reverse mapping */

    /* IRQ lines */
    qemu_irq irq;           /* SPMI interrupt */

    /* State */
    bool enabled;
    uint32_t version;       /* SPMI Controller version */
};

/* SPMI Bus Operations */
typedef struct SPMIBusOps {
    uint64_t (*read)(void *opaque, uint8_t slave_id, uint16_t addr,
                     unsigned size);
    void (*write)(void *opaque, uint8_t slave_id, uint16_t addr,
                  uint64_t value, unsigned size);
} SPMIBusOps;

/* SPMI Slave Registration */
void spmi_register_slave(SPMIControllerState *controller, uint8_t slave_id,
                        DeviceState *device, const char *name,
                        uint64_t (*read_cb)(void *opaque, uint16_t addr,
                                            unsigned size),
                        void (*write_cb)(void *opaque, uint16_t addr,
                                         uint64_t value, unsigned size),
                        void *opaque);
void spmi_register_slave_simple(SPMIControllerState *controller,
                               uint8_t slave_id,
                               DeviceState *device, const char *name);
void spmi_unregister_slave(SPMIControllerState *controller, uint8_t slave_id);

/* SPMI Transaction Functions */
uint64_t spmi_read_slave(SPMIControllerState *controller, uint8_t slave_id,
                        uint16_t addr, unsigned size);
void spmi_write_slave(SPMIControllerState *controller, uint8_t slave_id,
                     uint16_t addr, uint64_t value, unsigned size);

#endif /* HW_MISC_SPMI_CONTROLLER_H */
