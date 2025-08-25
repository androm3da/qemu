/*
 * Hexagon TLB QOM Object
 *
 * Copyright(c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/hexagon/hexagon_tlb.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "hw/resettable.h"
#include "migration/vmstate.h"
#include "qom/object.h"
#include "target/hexagon/cpu.h"
#include "target/hexagon/hex_regs.h"
#include "target/hexagon/reg_fields.h"
#include "qemu/log.h"
#include "qemu/qemu-print.h"

/* Forward declaration */
static uint32_t hexagon_tlb_lookup_by_asid(HexagonCPU *cpu, uint32_t asid,
                                            uint32_t va);

static void hexagon_tlb_init(Object *obj)
{
    HexagonTLBState *s = HEXAGON_TLB(obj);

    /* Initialize TLB entries to invalid state */
    memset(s->entries, 0, sizeof(s->entries));
    s->num_entries = MAX_TLB_ENTRIES;
    s->lock_state = 0;
    s->lock_count = 0;
}

void hexagon_tlb_write_entry(HexagonCPU *cpu, uint32_t index, uint64_t value)
{
    HexagonTLBState *s = cpu->tlb_obj;
    g_assert(s);
    g_assert(index < s->num_entries);

    s->entries[index] = value;
    /* TLB write traced elsewhere */
}

uint64_t hexagon_tlb_read_entry(HexagonCPU *cpu, uint32_t index)
{
    HexagonTLBState *s = cpu->tlb_obj;
    g_assert(s);
    g_assert(index < s->num_entries);

    uint64_t value = s->entries[index];
    /* TLB read tracing can be added if needed */
    return value;
}

uint32_t hexagon_tlb_lookup(HexagonCPU *cpu, uint32_t ssr, uint32_t va)
{
    HexagonTLBState *s = cpu->tlb_obj;
    g_assert(s);

    /* Extract ASID from SSR register */
    uint32_t asid = (ssr >> 8) & 0xff;  /* SSR_ASID field */
    return hexagon_tlb_lookup_by_asid(cpu, asid, va);
}

static uint32_t hexagon_tlb_lookup_by_asid(HexagonCPU *cpu, uint32_t asid,
                                            uint32_t va)
{
    HexagonTLBState *s = cpu->tlb_obj;
    g_assert(s);

    uint32_t not_found = 0x80000000;
    uint32_t idx = not_found;
    CPUHexagonState *env = cpu_env(CPU(cpu));

    for (uint32_t i = 0; i < s->num_entries; i++) {
        uint64_t entry = s->entries[i];
        if (hex_tlb_entry_match_noperm(entry, asid, va)) {
            if (idx != not_found) {
                env->cause_code = HEX_CAUSE_IMPRECISE_MULTI_TLB_MATCH;
                break;
            }
            idx = i;
        }
    }

    if (idx == not_found) {
        qemu_log_mask(CPU_LOG_MMU, "%s: 0x%x, 0x%08x => NOT FOUND\n",
                      __func__, asid, va);
    } else {
        qemu_log_mask(CPU_LOG_MMU, "%s: 0x%x, 0x%08x => %d\n",
                      __func__, asid, va, idx);
    }

    /* TLB lookup tracing can be added if needed */
    return idx;
}

bool hexagon_tlb_find_match(HexagonCPU *cpu, target_ulong va,
                           MMUAccessType access_type, hwaddr *pa, int *prot,
                           int *size, int32_t *excp, int mmu_idx)
{
    HexagonTLBState *s = cpu->tlb_obj;
    g_assert(s);

    /* TODO: Move your existing hex_tlb_find_match implementation here */
    /* This is a placeholder */
    return false;
}

int hexagon_tlb_check_overlap(HexagonCPU *cpu, uint64_t entry, uint64_t index)
{
    HexagonTLBState *s = cpu->tlb_obj;
    g_assert(s);

    /* TODO: Move your existing hex_tlb_check_overlap implementation here */
    /* This is a placeholder */
    return 0;
}

void hexagon_tlb_lock(HexagonCPU *cpu)
{
    HexagonTLBState *s = cpu->tlb_obj;
    g_assert(s);

    s->lock_count++;
    s->lock_state = 1;
    /* TLB lock tracing handled elsewhere */
}

void hexagon_tlb_unlock(HexagonCPU *cpu)
{
    HexagonTLBState *s = cpu->tlb_obj;
    g_assert(s);

    if (s->lock_count > 0) {
        s->lock_count--;
        if (s->lock_count == 0) {
            s->lock_state = 0;
        }
    }
    /* TLB unlock tracing handled elsewhere */
}

void hexagon_tlb_dump(HexagonCPU *cpu)
{
    HexagonTLBState *s = cpu->tlb_obj;
    g_assert(s);

    qemu_printf("=== Hexagon TLB Dump ===\n");
    qemu_printf("Entries: %u, Lock State: %u, Lock Count: %u\n",
                s->num_entries, s->lock_state, s->lock_count);

    for (uint32_t i = 0; i < s->num_entries; i++) {
        if (s->entries[i] != 0) {
            qemu_printf("TLB[%u]: 0x%016" PRIx64 "\n", i, s->entries[i]);
        }
    }
    qemu_printf("======================\n");
}

static void do_hexagon_tlb_reset(HexagonTLBState *s)
{
    g_assert(s);

    /* Reset all TLB entries to invalid */
    memset(s->entries, 0, sizeof(s->entries));
    s->lock_state = 0;
    s->lock_count = 0;
}

static void hexagon_tlb_realize(DeviceState *dev, Error **errp)
{
    /* Nothing specific needed for realization */
}

void hexagon_tlb_reset(HexagonCPU *cpu)
{
    do_hexagon_tlb_reset(cpu->tlb_obj);
}

static void hexagon_tlb_reset_hold(Object *obj, ResetType type)
{
    HexagonTLBState *s = HEXAGON_TLB(obj);
    do_hexagon_tlb_reset(s);
}

static const VMStateDescription vmstate_hexagon_tlb = {
    .name = "hexagon_tlb",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]){
        VMSTATE_UINT64_ARRAY(entries, HexagonTLBState, MAX_TLB_ENTRIES),
        VMSTATE_UINT32(num_entries, HexagonTLBState),
        VMSTATE_UINT32(lock_state, HexagonTLBState),
        VMSTATE_UINT32(lock_count, HexagonTLBState),
        VMSTATE_END_OF_LIST()
    }
};

static const Property hexagon_tlb_properties[] = {
    DEFINE_PROP_UINT32("num-entries", HexagonTLBState, num_entries,
                       MAX_TLB_ENTRIES),
};

static void hexagon_tlb_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = hexagon_tlb_realize;
    rc->phases.hold = hexagon_tlb_reset_hold;
    dc->vmsd = &vmstate_hexagon_tlb;
    dc->user_creatable = false;
    device_class_set_props(dc, hexagon_tlb_properties);
}

static const TypeInfo hexagon_tlb_info = {
    .name = TYPE_HEXAGON_TLB,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(HexagonTLBState),
    .instance_init = hexagon_tlb_init,
    .class_init = hexagon_tlb_class_init,
};

static void hexagon_tlb_register_types(void)
{
    type_register_static(&hexagon_tlb_info);
}

type_init(hexagon_tlb_register_types)
