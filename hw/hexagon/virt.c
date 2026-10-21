/*
 * Hexagon virt emulation
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/hexagon/virt.h"
#include "hw/hexagon/hexagon.h"
#include "hw/hexagon/hex-board.h"
#include "hw/hexagon/hex-subsys.h"
#include "hw/core/loader.h"
#include "qemu/error-report.h"
#include "qemu/target-info.h"
#include "qemu/units.h"
#include "machine_cfg_v68n_1024.h.inc"
#include "machine_cfg_v81_virt.h.inc"
#include "system/device_tree.h"

enum {
    VIRT_UART0,
    VIRT_MMIO,
    VIRT_FDT,
};

/*
 * Virtio IRQs run from VIRTIO_IRQ_BASE to
 * VIRTIO_IRQ_BASE + VIRTIO_DEV_COUNT - 1
 */
static const int VIRTIO_IRQ_BASE = 16;
static const int VIRT_UART0_IRQ = 15;

static const MemMapEntry base_memmap[] = {
    [VIRT_UART0] = { 0x10000000, 0x00001000 },
    [VIRT_MMIO] = { 0x11000000, 0x00001000 },
    [VIRT_FDT] = { 0x99800000, 0x00400000 },
};


static uint64_t load_kernel(const HexagonVirtMachineState *vms)
{
    return hex_board_load_elf(MACHINE(vms)->kernel_filename, NULL, NULL, NULL);
}

static uint64_t load_bios(HexagonVirtMachineState *vms)
{
    MachineState *ms = MACHINE(vms);
    uint64_t bios_addr = 0x0;  /* Load BIOS at reset vector address 0x0 */
    int bios_size;

    bios_size = load_image_targphys(ms->firmware ?: "",
                                    bios_addr, 64 * 1024, NULL);
    if (bios_size < 0) {
        error_report("Could not load BIOS '%s'", ms->firmware ?: "");
        exit(1);
    }

    return bios_addr;  /* Return entry point at address 0x0 */
}

static void virt_init(MachineState *ms)
{
    HexagonVirtMachineState *vms = HEXAGON_VIRT_MACHINE(ms);
    const HexagonVirtMachineInfo *info =
        HEXAGON_VIRT_MACHINE_GET_CLASS(ms)->info;
    const struct hexagon_machine_config *m_cfg = info->cfg;
    int32_t l2vic_phandle;
    uint64_t start = 0;
    bool have_start = false;

    hex_board_create_fdt(ms, "hexagon-virt,qemu", "qcom,sm8150",
                         &vms->fdt_size);
    qemu_fdt_setprop_string(ms->fdt, "/chosen", "bootargs", ms->kernel_cmdline);
    qemu_fdt_setprop_string(ms->fdt, "/soc", "compatible", "simple-bus");

    hex_subsys_create(&vms->parent_obj, m_cfg, info->rev);
    hex_board_create_tcm(m_cfg, &vms->tcm);
    hex_board_fdt_add_hvx(ms, m_cfg);

    l2vic_phandle = hex_board_fdt_add_l2vic(ms, m_cfg);
    hex_board_create_virtio(ms, base_memmap[VIRT_MMIO].base,
                            base_memmap[VIRT_MMIO].size, VIRTIO_IRQ_BASE,
                            VIRTIO_DEV_COUNT, l2vic_phandle);

    if (ms->kernel_filename) {
        start = load_kernel(vms);
        have_start = true;
    } else if (ms->firmware) {
        start = load_bios(vms);
        have_start = true;
    }
    hex_board_create_cpus(ms, have_start, start);

    hex_board_fdt_add_cpus(ms);
    hex_board_create_uart(ms, base_memmap[VIRT_UART0].base,
                          base_memmap[VIRT_UART0].size, VIRT_UART0_IRQ,
                          l2vic_phandle);

    hex_board_load_fdt(ms, vms->fdt_size, base_memmap[VIRT_FDT].base);
}


static void virt_base_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->init = virt_init;
    mc->default_ram_size = 4 * GiB;
    mc->is_default = false;
    mc->default_kernel_irqchip_split = false;
    mc->block_default_type = IF_VIRTIO;
    mc->default_boot_order = NULL;
    mc->no_cdrom = 1;
    mc->numa_mem_supported = false;
    mc->default_nic = "virtio-mmio-bus";
}

static void virt_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    HexagonVirtMachineClass *vc = HEXAGON_VIRT_MACHINE_CLASS(oc);
    const HexagonVirtMachineInfo *info = data;

    vc->info = info;
    mc->desc = info->desc;
    mc->default_cpu_type = info->cpu_type;
    mc->default_cpus = info->cpus;
    mc->max_cpus = info->cpus;
}

static const HexagonVirtMachineInfo v68_virt_info = {
    .desc = "Hexagon Virtual Machine with a v68 core",
    .cpu_type = HEXAGON_CPU_TYPE_NAME("v68"),
    .cpus = 8,
    .rev = v68_rev,
    .cfg = &v68n_1024,
};

/* A v81 core with the most threads and HVX contexts that the CPU has. */
static const HexagonVirtMachineInfo v81_virt_info = {
    .desc = "Hexagon Virtual Machine with a v81 core",
    .cpu_type = HEXAGON_CPU_TYPE_NAME("v81"),
    .cpus = 16,
    .rev = v81_rev,
    .cfg = &v81_virt,
};

static const TypeInfo virt_machine_types[] = {
    {
        .name = TYPE_HEXAGON_VIRT_MACHINE,
        .parent = TYPE_HEXAGON_COMMON_MACHINE,
        .instance_size = sizeof(HexagonVirtMachineState),
        .class_size = sizeof(HexagonVirtMachineClass),
        .class_init = virt_base_class_init,
        .is_available = target_hexagon,
        .abstract = true,
    }, {
        .name = MACHINE_TYPE_NAME("v68_virt"),
        .parent = TYPE_HEXAGON_VIRT_MACHINE,
        .class_init = virt_class_init,
        .class_data = &v68_virt_info,
    }, {
        .name = MACHINE_TYPE_NAME("v81_virt"),
        .parent = TYPE_HEXAGON_VIRT_MACHINE,
        .class_init = virt_class_init,
        .class_data = &v81_virt_info,
    },
};

DEFINE_TYPES(virt_machine_types)
