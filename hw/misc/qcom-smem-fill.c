/*
 * Qualcomm Shared Memory (SMEM) region initializer
 *
 * Pure data-layout logic: no QEMU device infrastructure dependencies.
 * This file is compiled into both the device model and the test binary.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/misc/qcom-smem.h"

/*
 * SA8775P partition configuration
 * Extracted from live target memory dump.
 * Entry [0] is the ptable page itself; entry [15] is the SMEM header page.
 * Entries [1]-[14] are the inter-processor communication partitions.
 */
static const QcomSMEMPartitionConfig sa8775p_partitions[] = {
    /* [0] Partition table page (self-describing, flags=1, no cacheline) */
    { .offset = 0x1ff000, .size = 0x01000, .flags = 1,
      .host0 = 0xffff, .host1 = 0xffff, .cacheline = 0 },
    /* [1] APPS <-> ADSP (host 2) */
    { .offset = 0x1cf000, .size = 0x30000, .flags = 3,
      .host0 = SMEM_HOST_APPS, .host1 = SMEM_HOST_ADSP, .cacheline = 32 },
    /* [2] APPS <-> SPSS (host 5) */
    { .offset = 0x1af000, .size = 0x20000, .flags = 3,
      .host0 = SMEM_HOST_APPS, .host1 = SMEM_HOST_SPSS, .cacheline = 32 },
    /* [3] APPS <-> CDSP1 (host 12) */
    { .offset = 0x18f000, .size = 0x20000, .flags = 3,
      .host0 = SMEM_HOST_APPS, .host1 = SMEM_HOST_CDSP1, .cacheline = 32 },
    /* [4] APPS <-> NSP0 (host 17) */
    { .offset = 0x16f000, .size = 0x20000, .flags = 3,
      .host0 = SMEM_HOST_APPS, .host1 = SMEM_HOST_NSP0, .cacheline = 32 },
    /* [5] APPS <-> NSP1 (host 18) */
    { .offset = 0x14f000, .size = 0x20000, .flags = 3,
      .host0 = SMEM_HOST_APPS, .host1 = SMEM_HOST_NSP1, .cacheline = 32 },
    /* [6] ADSP <-> NSP0 */
    { .offset = 0x12f000, .size = 0x20000, .flags = 3,
      .host0 = SMEM_HOST_ADSP, .host1 = SMEM_HOST_NSP0, .cacheline = 32 },
    /* [7] ADSP <-> NSP1 */
    { .offset = 0x10f000, .size = 0x20000, .flags = 3,
      .host0 = SMEM_HOST_ADSP, .host1 = SMEM_HOST_NSP1, .cacheline = 32 },
    /* [8] NSP0 <-> NSP1 */
    { .offset = 0x0ef000, .size = 0x20000, .flags = 3,
      .host0 = SMEM_HOST_NSP0, .host1 = SMEM_HOST_NSP1, .cacheline = 32 },
    /* [9] APPS_NS <-> ADSP */
    { .offset = 0x0eb000, .size = 0x4000, .flags = 3,
      .host0 = SMEM_HOST_APPS_NS, .host1 = SMEM_HOST_ADSP, .cacheline = 32 },
    /* [10] APPS_NS <-> SPSS */
    { .offset = 0x0e7000, .size = 0x4000, .flags = 3,
      .host0 = SMEM_HOST_APPS_NS, .host1 = SMEM_HOST_SPSS, .cacheline = 32 },
    /* [11] APPS_NS <-> CDSP1 */
    { .offset = 0x0e3000, .size = 0x4000, .flags = 3,
      .host0 = SMEM_HOST_APPS_NS, .host1 = SMEM_HOST_CDSP1, .cacheline = 32 },
    /* [12] APPS_NS <-> NSP0 */
    { .offset = 0x0df000, .size = 0x4000, .flags = 3,
      .host0 = SMEM_HOST_APPS_NS, .host1 = SMEM_HOST_NSP0, .cacheline = 32 },
    /* [13] APPS_NS <-> NSP1 */
    { .offset = 0x0db000, .size = 0x4000, .flags = 3,
      .host0 = SMEM_HOST_APPS_NS, .host1 = SMEM_HOST_NSP1, .cacheline = 32 },
    /* [14] Global partition */
    { .offset = 0x001000, .size = 0xda000, .flags = 3,
      .host0 = SMEM_GLOBAL_HOST, .host1 = SMEM_GLOBAL_HOST, .cacheline = 32 },
    /* [15] SMEM header page (self-describing, flags=3, no cacheline) */
    { .offset = 0x000000, .size = 0x01000, .flags = 3,
      .host0 = 0xffff, .host1 = 0xffff, .cacheline = 0 },
};

/*
 * Target configurations
 */

/*
 * Additional version[] indices set by the SA8775P SBL beyond [0] and [7].
 * These correspond to processor slots acknowledged by the bootloader.
 */
static const uint8_t sa8775p_extra_version_indices[] = { 8, 15, 18 };

/*
 * SA8775P PMIC array (8 PMICs: alternating PM8775/PM8775A, all die_rev 2.0)
 */
static const QcomSMEMPmicEntry sa8775p_pmic_entries[] = {
    { 0x0001004e, 0x00020000 },  /* PM8775  */
    { 0x0001004f, 0x00020000 },  /* PM8775A */
    { 0x0001004e, 0x00020000 },  /* PM8775  */
    { 0x0001004f, 0x00020000 },  /* PM8775A */
    { 0x0001004e, 0x00020000 },  /* PM8775  */
    { 0x0001004f, 0x00020000 },  /* PM8775A */
    { 0x0001004e, 0x00020000 },  /* PM8775  */
    { 0x0001004f, 0x00020000 },  /* PM8775A */
};

static const QcomSMEMTargetConfig smem_target_configs[] = {
    {
        .name = "sa8775p",
        .soc_id = 533,              /* 0x215 */
        .soc_version = 0x00020000,  /* Major 2, Minor 0 */
        .hw_platform = 0x25,        /* 37 */
        .hw_platform_subtype = 1,
        .platform_version = 0x00010000,
        .raw_id = 465,              /* 0x1d1 */
        .raw_version = 1,
        .fmt_version = 15,
        .smem_size = 0x200000,      /* 2MB */
        .smem_base_addr = 0x90900000,
        .num_items = 647,
        .free_offset = 0x1000,      /* Legacy SBL value (page-aligned) */
        .pmic_model = 0x0001004e,   /* PM8775 */
        .pmic_die_rev = 0x00020000,
        .pmic_model_1 = 0x0001004f,
        .pmic_die_rev_1 = 0x00020000,
        .pmic_model_2 = 0x0001004e,
        .pmic_die_rev_2 = 0x00020000,
        .num_pmics = 8,
        .pmic_array_offset = 0xf0,
        .pmic_entries = sa8775p_pmic_entries,
        .chip_family = 0x83,
        .raw_device_num = 0x19,
        .nproduct_id = 0x440,
        .num_clusters = 1,
        .ncluster_array_offset = 0xb0,
        .num_subset_parts = 15,
        .nsubset_parts_array_offset = 0xb4,
        .extra_version_indices = sa8775p_extra_version_indices,
        .num_extra_version_indices = ARRAY_SIZE(sa8775p_extra_version_indices),
        .partitions = sa8775p_partitions,
        .num_partitions = ARRAY_SIZE(sa8775p_partitions),
    },
};

const QcomSMEMTargetConfig *qcom_smem_find_target_config(const char *name)
{
    size_t i;

    for (i = 0; i < ARRAY_SIZE(smem_target_configs); i++) {
        if (strcmp(smem_target_configs[i].name, name) == 0) {
            return &smem_target_configs[i];
        }
    }
    return NULL;
}

/*
 * Helper to write partition table entry
 */
static void write_ptable_entry(void *base, uint32_t index,
                               const QcomSMEMPartitionConfig *cfg)
{
    size_t offset = sizeof(SMEMPtable) + index * sizeof(SMEMPtableEntry);
    SMEMPtableEntry *entry = (SMEMPtableEntry *)((uint8_t *)base + offset);

    entry->offset = cpu_to_le32(cfg->offset);
    entry->size = cpu_to_le32(cfg->size);
    entry->flags = cpu_to_le32(cfg->flags);
    entry->host0 = cpu_to_le16(cfg->host0);
    entry->host1 = cpu_to_le16(cfg->host1);
    entry->cacheline = cpu_to_le32(cfg->cacheline);
    memset(entry->reserved, 0, sizeof(entry->reserved));
}

/*
 * Initialize a partition header
 */
static void init_partition_header(void *part_base,
                                  const QcomSMEMPartitionConfig *cfg)
{
    SMEMPartitionHeader *hdr = (SMEMPartitionHeader *)part_base;

    memcpy(hdr->magic, SMEM_PART_MAGIC, 4);
    hdr->host0 = cpu_to_le16(cfg->host0);
    hdr->host1 = cpu_to_le16(cfg->host1);
    hdr->size = cpu_to_le32(cfg->size);
    /* Initially no items allocated - free offset starts after header */
    hdr->offset_free_uncached = cpu_to_le32(sizeof(SMEMPartitionHeader));
    hdr->offset_free_cached = cpu_to_le32(cfg->size);
    memset(hdr->reserved, 0, sizeof(hdr->reserved));
}

/*
 * Allocate an item in a partition
 * Returns pointer to the data area, or NULL on failure
 */
static void *partition_alloc_item(void *part_base, uint16_t item,
                                  uint32_t size)
{
    SMEMPartitionHeader *hdr = (SMEMPartitionHeader *)part_base;
    uint32_t free_uncached = le32_to_cpu(hdr->offset_free_uncached);
    uint32_t free_cached = le32_to_cpu(hdr->offset_free_cached);
    uint32_t alloc_size = (size + 7) & ~7;
    uint32_t total_size = sizeof(SMEMPrivateEntry) + alloc_size;
    SMEMPrivateEntry *entry;

    /* Check if there's room */
    if (free_uncached + total_size > free_cached) {
        return NULL;
    }

    /* Write the entry header */
    entry = (SMEMPrivateEntry *)((uint8_t *)part_base + free_uncached);
    entry->canary = cpu_to_le16(SMEM_PRIVATE_CANARY);
    entry->item = cpu_to_le16(item);
    entry->size = cpu_to_le32(alloc_size);
    entry->padding_data = cpu_to_le16(alloc_size - size);
    entry->padding_hdr = 0;
    entry->reserved = 0;

    /* Update free offset */
    hdr->offset_free_uncached = cpu_to_le32(free_uncached + total_size);

    /* Return pointer to data area */
    return (uint8_t *)part_base + free_uncached + sizeof(SMEMPrivateEntry);
}

/*
 * Initialize socinfo item (item 137)
 */
static void init_socinfo(void *data, const QcomSMEMTargetConfig *cfg)
{
    SMEMSocinfo *info = (SMEMSocinfo *)data;
    uint32_t i;

    memset(info, 0, sizeof(*info));

    info->fmt = cpu_to_le32(cfg->fmt_version);
    info->id = cpu_to_le32(cfg->soc_id);
    info->ver = cpu_to_le32(cfg->soc_version);
    /* build_id left empty - filled at runtime */
    info->raw_id = cpu_to_le32(cfg->raw_id);
    info->raw_ver = cpu_to_le32(cfg->raw_version);
    info->hw_plat = cpu_to_le32(cfg->hw_platform);
    info->plat_ver = cpu_to_le32(cfg->platform_version);
    info->hw_plat_subtype = cpu_to_le32(cfg->hw_platform_subtype);
    info->pmic_model = cpu_to_le32(cfg->pmic_model);
    info->pmic_die_rev = cpu_to_le32(cfg->pmic_die_rev);
    info->pmic_model_1 = cpu_to_le32(cfg->pmic_model_1);
    info->pmic_die_rev_1 = cpu_to_le32(cfg->pmic_die_rev_1);
    info->pmic_model_2 = cpu_to_le32(cfg->pmic_model_2);
    info->pmic_die_rev_2 = cpu_to_le32(cfg->pmic_die_rev_2);
    info->foundry_id = cpu_to_le32(3);
    info->serial_num = 0;   /* device-specific, filled at runtime */
    info->num_pmics = cpu_to_le32(cfg->num_pmics);
    info->pmic_array_offset = cpu_to_le32(cfg->pmic_array_offset);
    info->chip_family = cpu_to_le32(cfg->chip_family);
    info->raw_device_family = cpu_to_le32(0x06);
    info->raw_device_num = cpu_to_le32(cfg->raw_device_num);
    info->nproduct_id = cpu_to_le32(cfg->nproduct_id);
    /* chip_id left empty - filled at runtime */
    info->num_clusters = cpu_to_le32(cfg->num_clusters);
    info->ncluster_array_offset = cpu_to_le32(cfg->ncluster_array_offset);
    info->num_subset_parts = cpu_to_le32(cfg->num_subset_parts);
    info->nsubset_parts_array_offset =
        cpu_to_le32(cfg->nsubset_parts_array_offset);

    /* Populate the inline PMIC array at pmic_array_offset */
    for (i = 0; i < cfg->num_pmics && i < ARRAY_SIZE(info->pmic_array); i++) {
        info->pmic_array[i].model =
            cpu_to_le32(cfg->pmic_entries[i].model);
        info->pmic_array[i].die_rev =
            cpu_to_le32(cfg->pmic_entries[i].die_rev);
    }
}

/*
 * Initialize SMP2P structure
 */
static void init_smp2p(void *data, uint16_t local_pid, uint16_t remote_pid)
{
    SMP2PSmemItem *smp2p = (SMP2PSmemItem *)data;

    memset(smp2p, 0, sizeof(*smp2p));
    smp2p->magic = cpu_to_le32(0x504d5324);  /* "$SMP" in little endian */
    smp2p->version = 1;
    smp2p->local_pid = cpu_to_le16(local_pid);
    smp2p->remote_pid = cpu_to_le16(remote_pid);
    smp2p->total_entries = cpu_to_le16(SMP2P_MAX_ENTRIES);
    smp2p->valid_entries = 0;
    smp2p->flags = 0;
}

/*
 * Find the global partition in the configuration
 */
static const QcomSMEMPartitionConfig *find_global_partition(
    const QcomSMEMTargetConfig *cfg)
{
    uint32_t i;

    for (i = 0; i < cfg->num_partitions; i++) {
        if (cfg->partitions[i].host0 == SMEM_GLOBAL_HOST &&
            cfg->partitions[i].host1 == SMEM_GLOBAL_HOST) {
            return &cfg->partitions[i];
        }
    }
    return NULL;
}

/*
 * Find partition for a specific host pair
 */
static const QcomSMEMPartitionConfig *find_partition(
    const QcomSMEMTargetConfig *cfg, uint16_t host0, uint16_t host1)
{
    uint32_t i;

    for (i = 0; i < cfg->num_partitions; i++) {
        if ((cfg->partitions[i].host0 == host0 &&
             cfg->partitions[i].host1 == host1) ||
            (cfg->partitions[i].host0 == host1 &&
             cfg->partitions[i].host1 == host0)) {
            return &cfg->partitions[i];
        }
    }
    return NULL;
}

void qcom_smem_fill_region(void *base, uint32_t smem_size,
                           const QcomSMEMTargetConfig *cfg)
{
    void *ptable_base;
    void *smem_info;
    void *global_part;
    void *item_data;
    void *part_base;
    SMEMHeader *hdr;
    SMEMPtable *ptable;
    SMEMInfo *info;
    const QcomSMEMPartitionConfig *global_cfg;
    const QcomSMEMPartitionConfig *pcfg;
    uint16_t glink_remotes[] = {
        SMEM_HOST_ADSP, SMEM_HOST_SPSS
    };
    struct {
        uint16_t remote;
        uint16_t item;
    } smp2p_configs[] = {
        { SMEM_HOST_ADSP, SMEM_SMP2P_APPS_MPSS },
        { SMEM_HOST_SPSS, SMEM_SMP2P_APPS_ADSP },
    };
    uint32_t i;

    /* Clear entire region */
    memset(base, 0, smem_size);

    /*
     * Initialize SMEM header at offset 0
     * For v12, this is mostly unused but must be present
     */
    hdr = (SMEMHeader *)base;
    hdr->version[0] = cpu_to_le32((SMEM_GLOBAL_PART_VERSION << 16) | 0);
    hdr->version[SMEM_MASTER_SBL_VERSION_INDEX] =
        cpu_to_le32((SMEM_GLOBAL_PART_VERSION << 16) | 0);
    for (i = 0; i < cfg->num_extra_version_indices; i++) {
        hdr->version[cfg->extra_version_indices[i]] =
            cpu_to_le32((SMEM_GLOBAL_PART_VERSION << 16) | 0);
    }
    hdr->initialized = cpu_to_le32(1);
    hdr->free_offset = cpu_to_le32(cfg->free_offset);
    hdr->available = 0;  /* v12 doesn't use global heap */
    hdr->reserved = 0;

    /*
     * Initialize partition table at end - 4KB
     */
    ptable_base = (uint8_t *)base + smem_size - 4096;
    ptable = (SMEMPtable *)ptable_base;
    memcpy(ptable->magic, SMEM_PTABLE_MAGIC, 4);
    ptable->version = cpu_to_le32(1);
    ptable->num_entries = cpu_to_le32(cfg->num_partitions);
    memset(ptable->reserved, 0, sizeof(ptable->reserved));

    /* Write partition table entries */
    for (i = 0; i < cfg->num_partitions; i++) {
        write_ptable_entry(ptable_base, i, &cfg->partitions[i]);
    }

    /*
     * Initialize SMEM_INFO structure after partition entries
     */
    smem_info = (uint8_t *)ptable_base + sizeof(SMEMPtable) +
                cfg->num_partitions * sizeof(SMEMPtableEntry);
    info = (SMEMInfo *)smem_info;
    memcpy(info->magic, SMEM_INFO_MAGIC, 4);
    info->size = cpu_to_le32(cfg->smem_size);
    info->base_addr = cpu_to_le32(cfg->smem_base_addr);
    info->reserved = 0;
    info->num_items = cpu_to_le16(cfg->num_items);

    /*
     * Initialize each partition
     */
    for (i = 0; i < cfg->num_partitions; i++) {
        pcfg = &cfg->partitions[i];
        /* Skip self-describing entries (ptable/header pages, host=0xffff) */
        if (pcfg->host0 == 0xffff && pcfg->host1 == 0xffff) {
            continue;
        }
        part_base = (uint8_t *)base + pcfg->offset;
        init_partition_header(part_base, pcfg);
    }

    /*
     * Allocate items in global partition
     */
    global_cfg = find_global_partition(cfg);
    if (global_cfg) {
        global_part = (uint8_t *)base + global_cfg->offset;

        /* Item 137: socinfo - REQUIRED */
        item_data = partition_alloc_item(global_part,
                                        SMEM_HW_SW_BUILD_ID,
                                        sizeof(SMEMSocinfo));
        if (item_data) {
            init_socinfo(item_data, cfg);
        }
    }

    /*
     * Allocate GLINK items in APPS<->ADSP and APPS<->SPSS partitions.
     * The SBL allocates these slots but leaves the data zero; the
     * firmware fills in the GLINK descriptor and FIFO at runtime.
     */
    for (i = 0; i < ARRAY_SIZE(glink_remotes); i++) {
        pcfg = find_partition(cfg, SMEM_HOST_APPS, glink_remotes[i]);
        if (pcfg) {
            part_base = (uint8_t *)base + pcfg->offset;
            /* Item 478: GLINK descriptor - allocated but data left zero */
            partition_alloc_item(part_base,
                                 SMEM_GLINK_NATIVE_XPRT_DESCRIPTOR,
                                 sizeof(GLinkDescriptor));
            /* Item 479: GLINK FIFO - allocated but data left zero */
            partition_alloc_item(part_base,
                                 SMEM_GLINK_NATIVE_XPRT_FIFO, 0x4000);
        }
    }

    /*
     * Allocate SMP2P items in APPS_NS<->remote partitions
     */
    for (i = 0; i < ARRAY_SIZE(smp2p_configs); i++) {
        pcfg = find_partition(cfg, SMEM_HOST_APPS_NS,
                              smp2p_configs[i].remote);
        if (pcfg) {
            part_base = (uint8_t *)base + pcfg->offset;
            item_data = partition_alloc_item(part_base,
                            smp2p_configs[i].item, sizeof(SMP2PSmemItem));
            if (item_data) {
                init_smp2p(item_data, SMEM_HOST_APPS_NS,
                           smp2p_configs[i].remote);
            }
        }
    }
}
