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
#include "hw/hexagon/hexagon_hvm.h"
#include "hw/core/loader.h"
#include "qemu/error-report.h"
#include "qemu/target-info.h"
#include "qemu/units.h"
#include "machine_cfg_v68n_1024.h.inc"
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


#define VIRT_KERNEL_ADDR 0xa0000000
static uint64_t kernel_translate(void *opaque, uint64_t addr)
{
    return addr + VIRT_KERNEL_ADDR;
}

/*
 * Load an HVM guest kernel at its fixed address, followed by the initramfs
 * and device tree.  Returns the guest entry point.
 */
static uint64_t load_hvm_linux(MachineState *ms,
                               const struct hexagon_machine_config *m_cfg,
                               hwaddr *fdt_addr)
{
    uint64_t entry, high;
    hwaddr ram_base = m_cfg->ddr_base;
    hwaddr guest_ram_end = MIN(ram_base + ms->ram_size,
                               VIRT_KERNEL_ADDR + 896 * MiB);

    if (ram_base > VIRT_KERNEL_ADDR ||
        guest_ram_end <= VIRT_KERNEL_ADDR + 32 * MiB) {
        error_report("insufficient RAM for the guest kernel");
        exit(1);
    }
    entry = hex_board_load_elf(ms->kernel_filename, kernel_translate, NULL,
                               &high);
    *fdt_addr = QEMU_ALIGN_UP(high + 16 * MiB, 4 * MiB);
    if (*fdt_addr + 4 * MiB >= guest_ram_end) {
        error_report("insufficient RAM for device tree");
        exit(1);
    }
    if (ms->initrd_filename) {
        hwaddr initrd = *fdt_addr + 4 * MiB;
        ssize_t size = load_image_targphys_as(ms->initrd_filename, initrd,
                                              guest_ram_end - initrd,
                                              &address_space_memory,
                                              &error_fatal);
        qemu_fdt_setprop_u64(ms->fdt, "/chosen", "linux,initrd-start", initrd);
        qemu_fdt_setprop_u64(ms->fdt, "/chosen", "linux,initrd-end",
                             initrd + size);
    }
    return entry;
}

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
    HexagonCommonMachineState *hms = HEXAGON_COMMON_MACHINE(ms);
    const struct hexagon_machine_config *m_cfg = &v68n_1024;
    int32_t l2vic_phandle;
    uint64_t start = 0;
    bool have_start = false;
    hwaddr fdt_addr = base_memmap[VIRT_FDT].base;

    if (hms->hvm_enabled && ms->firmware) {
        error_report("hvm=on runs the guest without a hypervisor firmware "
                     "(-bios)");
        exit(1);
    }

    hex_board_create_fdt(ms, "hexagon-virt,qemu", "qcom,sm8150",
                         &vms->fdt_size);
    qemu_fdt_setprop_string(ms->fdt, "/chosen", "bootargs", ms->kernel_cmdline);
    qemu_fdt_setprop_string(ms->fdt, "/soc", "compatible", "simple-bus");

    hex_subsys_create(&vms->parent_obj, m_cfg, v68_rev);
    hex_board_create_tcm(m_cfg, &vms->tcm);
    hex_board_fdt_add_hvx(ms, m_cfg);

    l2vic_phandle = hex_board_fdt_add_interrupt_controller(ms, m_cfg);
    hex_board_create_virtio(ms, base_memmap[VIRT_MMIO].base,
                            base_memmap[VIRT_MMIO].size, VIRTIO_IRQ_BASE,
                            VIRTIO_DEV_COUNT, l2vic_phandle);

    if (hms->hvm_enabled) {
        /* Without a kernel, a bare guest, which the user loads themselves. */
        if (ms->kernel_filename) {
            start = load_hvm_linux(ms, m_cfg, &fdt_addr) -
                    HEX_BOARD_KERNEL_VADDR +
                    VIRT_KERNEL_ADDR;
            have_start = true;
            hex_board_set_hvm_boot_args(hms, fdt_addr);
        }
    } else if (ms->kernel_filename) {
        start = load_kernel(vms);
        have_start = true;
    } else if (ms->firmware) {
        start = load_bios(vms);
        have_start = true;
    }
    hex_board_create_cpus(ms, have_start, start);

    hex_board_fdt_add_cpus(ms);
    if (hms->hvm_enabled && ms->kernel_filename) {
        static const char compat[] = "qcom,h2-timer\0hvm-timer";
        qemu_fdt_add_subnode(ms->fdt, "/soc/timer@ab000000");
        qemu_fdt_setprop(ms->fdt, "/soc/timer@ab000000", "compatible",
                         compat, sizeof(compat));
        qemu_fdt_setprop_cells(ms->fdt, "/soc/timer@ab000000", "reg",
                               0, HVM_TIMER_BASE, HVM_TIMER_SIZE);
        qemu_fdt_setprop_cells(ms->fdt, "/soc/timer@ab000000", "interrupts",
                                HVM_TIMER_INT, 0);
    }
    hex_board_create_uart(ms, base_memmap[VIRT_UART0].base,
                          base_memmap[VIRT_UART0].size, VIRT_UART0_IRQ,
                          l2vic_phandle);

    hex_board_load_fdt(ms, vms->fdt_size, fdt_addr);
}


static void virt_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Hexagon Virtual Machine";
    hex_board_add_hvm_property(oc);
    mc->init = virt_init;
    mc->default_cpu_type = HEXAGON_CPU_TYPE_NAME("v68");
    mc->default_ram_size = 4 * GiB;
    mc->max_cpus = 8;
    mc->default_cpus = 8;
    mc->is_default = false;
    mc->default_kernel_irqchip_split = false;
    mc->block_default_type = IF_VIRTIO;
    mc->default_boot_order = NULL;
    mc->no_cdrom = 1;
    mc->numa_mem_supported = false;
    mc->default_nic = "virtio-mmio-bus";
}


static const TypeInfo virt_machine_types[] = { {
    .name = TYPE_HEXAGON_VIRT_MACHINE,
    .parent = TYPE_HEXAGON_COMMON_MACHINE,
    .instance_size = sizeof(HexagonVirtMachineState),
    .class_init = virt_class_init,
    .is_available = target_hexagon,
} };

DEFINE_TYPES(virt_machine_types)
