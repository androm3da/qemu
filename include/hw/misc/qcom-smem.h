/*
 * Qualcomm Shared Memory (SMEM) Device Model
 *
 * SMEM is a shared memory region used for inter-processor communication
 * on Qualcomm SoCs. This device model creates the SMEM data structures
 * that would normally be initialized by the bootloader.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_QCOM_SMEM_H
#define HW_MISC_QCOM_SMEM_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_QCOM_SMEM "qcom-smem"
OBJECT_DECLARE_SIMPLE_TYPE(QcomSMEMState, QCOM_SMEM)

/*
 * SMEM version constants
 */
#define SMEM_GLOBAL_HEAP_VERSION    11
#define SMEM_GLOBAL_PART_VERSION    12
#define SMEM_MASTER_SBL_VERSION_INDEX 7

/*
 * Host IDs for SMEM partitions
 */
#define SMEM_HOST_APPS      0
#define SMEM_HOST_MPSS      1   /* Modem */
#define SMEM_HOST_ADSP      2   /* Audio DSP */
#define SMEM_HOST_SLPI      3   /* Sensor Low Power Island */
#define SMEM_HOST_CDSP      4   /* Compute DSP */
#define SMEM_HOST_SPSS      5   /* Secure Processor */
#define SMEM_HOST_GPS       6
#define SMEM_HOST_APPS_NS   7   /* Non-secure APPS */
#define SMEM_HOST_CDSP1     12  /* Compute DSP 1 */
#define SMEM_HOST_NSP0      17  /* Neural Signal Processor 0 */
#define SMEM_HOST_NSP1      18  /* Neural Signal Processor 1 */
#define SMEM_HOST_COUNT     25
#define SMEM_GLOBAL_HOST    0xfffe

/*
 * Magic numbers
 */
#define SMEM_PTABLE_MAGIC   "$TOC"
#define SMEM_PART_MAGIC     "$PRT"
#define SMEM_INFO_MAGIC     "SIII"
#define SMEM_SMP2P_MAGIC    "$SMP"
#define SMEM_PRIVATE_CANARY 0xa5a5

/*
 * Common SMEM item IDs
 */
#define SMEM_HW_SW_BUILD_ID         137
#define SMEM_AARM_PARTITION_TABLE   9
#define SMEM_SMSM_SHARED_STATE      85
#define SMEM_GLINK_NATIVE_XPRT_DESCRIPTOR 478
#define SMEM_GLINK_NATIVE_XPRT_FIFO 479
#define SMEM_SMP2P_APPS_MPSS        491
#define SMEM_SMP2P_APPS_ADSP        494

/*
 * SMEM item count
 */
#define SMEM_ITEM_COUNT     512
#define SMEM_ITEM_LAST_FIXED 8

/*
 * Data structures matching Linux kernel definitions
 */

/* Legacy proc_comm structure */
typedef struct SMEMProcComm {
    uint32_t command;
    uint32_t status;
    uint32_t params[2];
} QEMU_PACKED SMEMProcComm;

/* Global heap entry in table of contents */
typedef struct SMEMGlobalEntry {
    uint32_t allocated;
    uint32_t offset;
    uint32_t size;
    uint32_t aux_base;
} QEMU_PACKED SMEMGlobalEntry;

/* Main SMEM header at offset 0 */
typedef struct SMEMHeader {
    SMEMProcComm proc_comm[4];
    uint32_t version[32];
    uint32_t initialized;
    uint32_t free_offset;
    uint32_t available;
    uint32_t reserved;
    SMEMGlobalEntry toc[SMEM_ITEM_COUNT];
} QEMU_PACKED SMEMHeader;

/* Partition table entry */
typedef struct SMEMPtableEntry {
    uint32_t offset;
    uint32_t size;
    uint32_t flags;
    uint16_t host0;
    uint16_t host1;
    uint32_t cacheline;
    uint32_t reserved[7];
} QEMU_PACKED SMEMPtableEntry;

/* Partition table at end of SMEM region */
typedef struct SMEMPtable {
    uint8_t magic[4];
    uint32_t version;
    uint32_t num_entries;
    uint32_t reserved[5];
    /* Followed by SMEMPtableEntry entries[] */
} QEMU_PACKED SMEMPtable;

/* Partition header */
typedef struct SMEMPartitionHeader {
    uint8_t magic[4];
    uint16_t host0;
    uint16_t host1;
    uint32_t size;
    uint32_t offset_free_uncached;
    uint32_t offset_free_cached;
    uint32_t reserved[3];
} QEMU_PACKED SMEMPartitionHeader;

/* Private entry header for items in partitions */
typedef struct SMEMPrivateEntry {
    uint16_t canary;
    uint16_t item;
    uint32_t size;
    uint16_t padding_data;
    uint16_t padding_hdr;
    uint32_t reserved;
} QEMU_PACKED SMEMPrivateEntry;

/* SMEM info structure after partition table entries */
typedef struct SMEMInfo {
    uint8_t magic[4];
    uint32_t size;
    uint32_t base_addr;
    uint32_t reserved;
    uint16_t num_items;
} QEMU_PACKED SMEMInfo;

/*
 * SoC info structure (SMEM item 137)
 */
typedef struct SMEMSocinfo {
    uint32_t fmt;
    uint32_t id;
    uint32_t ver;
    char build_id[32];
    /* Version 2 */
    uint32_t raw_id;
    uint32_t raw_ver;
    /* Version 3 */
    uint32_t hw_plat;
    /* Version 4 */
    uint32_t plat_ver;
    /* Version 5 */
    uint32_t accessory_chip;
    /* Version 6 */
    uint32_t hw_plat_subtype;
    /* Version 7 */
    uint32_t pmic_model;
    uint32_t pmic_die_rev;
    /* Version 8 */
    uint32_t pmic_model_1;
    uint32_t pmic_die_rev_1;
    uint32_t pmic_model_2;
    uint32_t pmic_die_rev_2;
    /* Version 9 */
    uint32_t foundry_id;
    /* Version 10 */
    uint32_t serial_num;
    /* Version 11 */
    uint32_t num_pmics;
    uint32_t pmic_array_offset;
    /* Version 12 */
    uint32_t chip_family;
    uint32_t raw_device_family;
    uint32_t raw_device_num;
    /* Version 13 */
    uint32_t nproduct_id;
    char chip_id[32];
    /* Version 14 */
    uint32_t num_clusters;
    uint32_t ncluster_array_offset;
    uint32_t num_subset_parts;
    uint32_t nsubset_parts_array_offset;
    /* Version 15 */
    uint32_t nmodem_supported;
    /* Version 16+ */
    uint32_t feature_code;
    uint32_t pcode;
    uint32_t npartnamemap_offset;
    uint32_t nnum_partname_mapping;
    /* Version 17 */
    uint32_t oem_variant;
    /* Version 18 */
    uint32_t num_kvps;
    uint32_t kvps_offset;
    /* Version 19 */
    uint32_t num_func_clusters;
    uint32_t boot_cluster;
    uint32_t boot_core;
    /* Version 20+ (reserved/unknown, zero-filled) */
    uint32_t reserved2[6];
    /* Inline PMIC array at pmic_array_offset */
    struct {
        uint32_t model;
        uint32_t die_rev;
    } pmic_array[8];
} QEMU_PACKED SMEMSocinfo;

/*
 * SMP2P (Shared Memory Point-to-Point) structure
 */
#define SMP2P_MAX_ENTRIES 16
#define SMP2P_ENTRY_NAME_LEN 16

typedef struct SMP2PEntry {
    char name[SMP2P_ENTRY_NAME_LEN];
    uint32_t value;
} QEMU_PACKED SMP2PEntry;

typedef struct SMP2PSmemItem {
    uint32_t magic;
    uint8_t version;
    uint8_t features[3];    /* 24-bit features field */
    uint16_t local_pid;
    uint16_t remote_pid;
    uint16_t total_entries;
    uint16_t valid_entries;
    uint32_t flags;
    SMP2PEntry entries[SMP2P_MAX_ENTRIES];
} QEMU_PACKED SMP2PSmemItem;

/*
 * GLINK native transport descriptor (item 478)
 */
typedef struct GLinkDescriptor {
    uint32_t version;
    uint32_t features;
    uint32_t fifo_size;
    uint32_t reserved[5];
} QEMU_PACKED GLinkDescriptor;

/*
 * SMEM configuration for a specific target
 */
typedef struct QcomSMEMPartitionConfig {
    uint32_t offset;
    uint32_t size;
    uint32_t flags;
    uint16_t host0;
    uint16_t host1;
    uint32_t cacheline;
} QcomSMEMPartitionConfig;

typedef struct QcomSMEMPmicEntry {
    uint32_t model;
    uint32_t die_rev;
} QcomSMEMPmicEntry;

typedef struct QcomSMEMTargetConfig {
    const char *name;
    uint32_t soc_id;
    uint32_t soc_version;
    uint32_t hw_platform;
    uint32_t hw_platform_subtype;
    uint32_t platform_version;
    uint32_t raw_id;
    uint32_t raw_version;
    uint32_t fmt_version;
    uint32_t smem_size;
    uint32_t smem_base_addr;
    uint32_t num_items;
    uint32_t free_offset;
    uint32_t pmic_model;
    uint32_t pmic_die_rev;
    uint32_t pmic_model_1;
    uint32_t pmic_die_rev_1;
    uint32_t pmic_model_2;
    uint32_t pmic_die_rev_2;
    uint32_t num_pmics;
    uint32_t pmic_array_offset;
    const QcomSMEMPmicEntry *pmic_entries;
    uint32_t chip_family;
    uint32_t raw_device_num;
    uint32_t nproduct_id;
    uint32_t num_clusters;
    uint32_t ncluster_array_offset;
    uint32_t num_subset_parts;
    uint32_t nsubset_parts_array_offset;
    /* Extra version[] indices beyond [0] and [SMEM_MASTER_SBL_VERSION_INDEX] */
    const uint8_t *extra_version_indices;
    uint32_t num_extra_version_indices;
    const QcomSMEMPartitionConfig *partitions;
    uint32_t num_partitions;
} QcomSMEMTargetConfig;

/*
 * QEMU device state
 */
struct QcomSMEMState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion smem_region;
    void *smem_base;
    uint32_t smem_size;
    hwaddr map_addr;

    /* Target configuration */
    const QcomSMEMTargetConfig *target_config;
    char *target_name;
};

/*
 * Public API
 */

/**
 * qcom_smem_fill_region - Fill a buffer with SMEM data structures
 * @base: pointer to memory buffer of at least cfg->smem_size bytes
 * @smem_size: size of the buffer in bytes
 * @cfg: target configuration describing partition layout and SoC identity
 *
 * Initializes the buffer with the SMEM header, partition table, partition
 * headers, and well-known items (socinfo, G-Link descriptors, SMP2P).
 * Callable without any QEMU device infrastructure; useful for testing.
 */
void qcom_smem_fill_region(void *base, uint32_t smem_size,
                           const QcomSMEMTargetConfig *cfg);

/**
 * qcom_smem_find_target_config - Look up a target configuration by name
 * @name: target name string (e.g. "sa8775p")
 *
 * Returns: pointer to the matching QcomSMEMTargetConfig, or NULL if not found
 */
const QcomSMEMTargetConfig *qcom_smem_find_target_config(const char *name);

/**
 * qcom_smem_create - Create and initialize SMEM device
 * @target_name: Name of target configuration (e.g., "sa8775p")
 * @map_addr: Physical address to map SMEM region
 *
 * Returns: The created device state, or NULL on failure
 */
QcomSMEMState *qcom_smem_create(const char *target_name, hwaddr map_addr);

/**
 * qcom_smem_get_base - Get base address of SMEM region
 * @s: SMEM device state
 *
 * Returns: Pointer to SMEM memory region
 */
void *qcom_smem_get_base(QcomSMEMState *s);

/**
 * qcom_smem_get_size - Get size of SMEM region
 * @s: SMEM device state
 *
 * Returns: Size in bytes
 */
uint32_t qcom_smem_get_size(QcomSMEMState *s);

#endif /* HW_MISC_QCOM_SMEM_H */
