/*
 * QEMU L2VIC Interrupt Controller
 *
 * Arm PrimeCell PL190 Vector Interrupt Controller was used as a reference.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/bitmap.h"
#include "qemu/bitops.h"
#include "qemu/lockable.h"
#include "hw/intc/l2vic.h"
#include "trace.h"

static void write_word(uint32_t *bitmap, int word_offset, uint32_t val)
{
    bitmap[word_offset] = val;
}

static void clear_word(uint32_t *bitmap, int word_offset, uint32_t mask)
{
    bitmap[word_offset] &= ~mask;
}

static void set_word(uint32_t *bitmap, int word_offset, uint32_t mask)
{
    bitmap[word_offset] |= mask;
}

static uint32_t read_word(uint32_t *bitmap, int word_offset)
{
    return bitmap[word_offset];
}

#define TYPE_L2VIC "l2vic"
OBJECT_DECLARE_TYPE(L2VICState, L2VICClass, L2VIC)

#define SLICE_MAX (L2VIC_INTERRUPT_MAX / 32)

typedef struct L2VICState {
    SysBusDevice parent_obj;

    QemuMutex active;
    MemoryRegion iomem;
    MemoryRegion fast_iomem;
    /*
     * offset 0:vid group 0 etc, 10 bits in each group
     * are used:
     */
    uint32_t vid_group[4];
    /* Enable interrupt source */
    DECLARE_BITMAP32(int_enable, L2VIC_INTERRUPT_MAX);
    /* Interrupt asserted but not yet delivered (INT_PENDINGn) */
    DECLARE_BITMAP32(int_pending, L2VIC_INTERRUPT_MAX);
    /* Which enabled interrupt is active */
    DECLARE_BITMAP32(int_status, L2VIC_INTERRUPT_MAX);
    /* Edge or Level interrupt */
    DECLARE_BITMAP32(int_type, L2VIC_INTERRUPT_MAX);
    /* L2 interrupt group 0-3 0x600-0x7FF */
    DECLARE_BITMAP32(int_group_n0, L2VIC_INTERRUPT_MAX);
    DECLARE_BITMAP32(int_group_n1, L2VIC_INTERRUPT_MAX);
    DECLARE_BITMAP32(int_group_n2, L2VIC_INTERRUPT_MAX);
    DECLARE_BITMAP32(int_group_n3, L2VIC_INTERRUPT_MAX);
    qemu_irq irq[8];
} L2VICState;

typedef struct L2VICClass {
    SysBusDeviceClass parent_class;
} L2VICClass;

/*
 * Find out if this irq is associated with a group other than
 * the default group
 */
static uint32_t *get_int_group(L2VICState *s, int irq)
{
    int n = irq & 0x1f;
    if (n < 8) {
        return s->int_group_n0;
    }
    if (n < 16) {
        return s->int_group_n1;
    }
    if (n < 24) {
        return s->int_group_n2;
    }
    return s->int_group_n3;
}

static int find_slice(int irq)
{
    return irq / 32;
}

static int get_vid(L2VICState *s, int irq)
{
    uint32_t *group = get_int_group(s, irq);
    uint32_t slice = group[find_slice(irq)];
    /* Mask with 0x7 to remove the GRP:EN bit */
    uint32_t val = slice >> ((irq & 0x7) * 4);
    if (val & 0x8) {
        return val & 0x7;
    } else {
        return 0;
    }
}

static inline bool vid_active(L2VICState *s)
{
    /* scan all 1024 bits in int_status array */
    const uint32_t size = L2VIC_INTERRUPT_MAX;
    const uint32_t active_irq = find_first_bit32(s->int_status, size);
    return active_irq != size;
}

static bool l2vic_update(L2VICState *s, int irq)
{
    if (vid_active(s)) {
        return true;
    }

    bool pending = test_bit32(irq, s->int_pending);
    bool enable = test_bit32(irq, s->int_enable);
    if (pending && enable) {
        int vid = get_vid(s, irq);
        /*
         * vid indexes vid_group[] (4 entries) and selects the output line
         * s->irq[vid + 2]. The guest can program an arbitrary 3-bit group in
         * the INT_GRPn registers, so reject out-of-range values instead of
         * overrunning those arrays.
         */
        if (vid >= ARRAY_SIZE(s->vid_group)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: irq %d configured with invalid vid %d\n",
                          __func__, irq, vid);
            return false;
        }
        set_bit32(irq, s->int_status);
        clear_bit32(irq, s->int_pending);
        /*
         * Only auto-disable for edge-triggered interrupts (type=1).
         * Level-triggered interrupts (type=0, the default) keep their
         * enable bit set across deliveries — the firmware enables once
         * and expects the interrupt to remain enabled.
         */
        if (test_bit32(irq, s->int_type)) {
            clear_bit32(irq, s->int_enable);
        }
        s->vid_group[vid] = irq;

        /* already low: now call pulse */
        /*     pulse: calls qemu_upper() and then qemu_lower()) */
        qemu_irq_pulse(s->irq[vid + 2]);
        trace_l2vic_delivered(irq, vid);
        return true;
    }
    return false;
}

static void l2vic_update_all(L2VICState *s)
{
    for (int i = 0; i < L2VIC_INTERRUPT_MAX; i++) {
        if (l2vic_update(s, i)) {
            /* once vid is active, no-one else can set it until ciad */
            return;
        }
    }
}

/* Caller must hold s->active. */
static void l2vic_set_irq_locked(L2VICState *s, int irq, int level)
{
    if (level) {
        set_bit32(irq, s->int_pending);
    }
    l2vic_update(s, irq);
}

static void l2vic_set_irq(void *opaque, int irq, int level)
{
    L2VICState *s = (L2VICState *)opaque;
    QEMU_LOCK_GUARD(&s->active);
    l2vic_set_irq_locked(s, irq, level);
}

static void l2vic_write(void *opaque, hwaddr offset, uint64_t val,
                        unsigned size)
{
    L2VICState *s = (L2VICState *)opaque;
    QEMU_LOCK_GUARD(&s->active);
    trace_l2vic_reg_write((unsigned)offset, (uint32_t)val);

    if (offset >= L2VIC_INT_ENABLEn && offset < L2VIC_INT_ENABLE_CLEARn) {
        write_word(s->int_enable, (offset - L2VIC_INT_ENABLEn) >> 2, val);
    } else if (offset >= L2VIC_INT_ENABLE_CLEARn &&
               offset < L2VIC_INT_ENABLE_SETn) {
        clear_word(s->int_enable, (offset - L2VIC_INT_ENABLE_CLEARn) >> 2, val);
    } else if (offset >= L2VIC_INT_ENABLE_SETn && offset < L2VIC_INT_TYPEn) {
        set_word(s->int_enable, (offset - L2VIC_INT_ENABLE_SETn) >> 2, val);
    } else if (offset >= L2VIC_INT_TYPEn && offset < L2VIC_INT_TYPEn + 0x80) {
        write_word(s->int_type, (offset - L2VIC_INT_TYPEn) >> 2, val);
    } else if (offset >= L2VIC_INT_STATUSn && offset < L2VIC_INT_CLEARn) {
        write_word(s->int_status, (offset - L2VIC_INT_STATUSn) >> 2, val);
    } else if (offset >= L2VIC_INT_CLEARn && offset < L2VIC_SOFT_INTn) {
        clear_word(s->int_status, (offset - L2VIC_INT_CLEARn) >> 2, val);
    } else if (offset >= L2VIC_INT_PENDINGn &&
               offset < L2VIC_INT_PENDINGn + 0x80) {
        write_word(s->int_pending, (offset - L2VIC_INT_PENDINGn) >> 2, val);
    } else if (offset >= L2VIC_SOFT_INTn && offset < L2VIC_INT_PENDINGn) {
        set_word(s->int_enable, (offset - L2VIC_SOFT_INTn) >> 2, val);
        /*
         *  Need to reverse engineer the actual irq number.
         */
        int irq = find_first_bit((unsigned long *)&val,
                                 sizeof(val) * CHAR_BIT);
        hwaddr byteoffset = offset - L2VIC_SOFT_INTn;
        /*
         * find_first_bit() returns its size argument when no bit is set, so a
         * write of 0 has nothing to trigger; skip it rather than computing an
         * out-of-range irq number and indexing past the bitmaps.
         */
        if (irq != sizeof(val) * CHAR_BIT) {
            irq += byteoffset * 8;

            /* The soft-int interface only works with edge-triggered ints */
            if (test_bit32(irq, s->int_type)) {
                l2vic_set_irq_locked(s, irq, 1);
            }
        }
    } else if (offset >= L2VIC_INT_GRPn_0 && offset < L2VIC_INT_GRPn_1) {
        write_word(s->int_group_n0, (offset - L2VIC_INT_GRPn_0) >> 2, val);
    } else if (offset >= L2VIC_INT_GRPn_1 && offset < L2VIC_INT_GRPn_2) {
        write_word(s->int_group_n1, (offset - L2VIC_INT_GRPn_1) >> 2, val);
    } else if (offset >= L2VIC_INT_GRPn_2 && offset < L2VIC_INT_GRPn_3) {
        write_word(s->int_group_n2, (offset - L2VIC_INT_GRPn_2) >> 2, val);
    } else if (offset >= L2VIC_INT_GRPn_3 && offset < L2VIC_INT_GRPn_3 + 0x80) {
        write_word(s->int_group_n3, (offset - L2VIC_INT_GRPn_3) >> 2, val);
    } else {
        qemu_log_mask(LOG_UNIMP,
                      "%s: offset 0x%" HWADDR_PRIx " unimplemented\n",
                      __func__, offset);
    }
    l2vic_update_all(s);
}

static uint64_t l2vic_read(void *opaque, hwaddr offset, unsigned size)
{
    uint64_t value;
    L2VICState *s = (L2VICState *)opaque;
    QEMU_LOCK_GUARD(&s->active);

    if (offset >= L2VIC_INT_ENABLEn && offset < L2VIC_INT_ENABLE_CLEARn) {
        value = read_word(s->int_enable, (offset - L2VIC_INT_ENABLEn) >> 2);
    } else if (offset >= L2VIC_INT_ENABLE_CLEARn &&
               offset < L2VIC_INT_ENABLE_SETn) {
        value = 0;
    } else if (offset >= L2VIC_INT_ENABLE_SETn && offset < L2VIC_INT_TYPEn) {
        value = 0;
    } else if (offset >= L2VIC_INT_TYPEn && offset < L2VIC_INT_TYPEn + 0x80) {
        value = read_word(s->int_type, (offset - L2VIC_INT_TYPEn) >> 2);
    } else if (offset >= L2VIC_INT_STATUSn && offset < L2VIC_INT_CLEARn) {
        value = read_word(s->int_status, (offset - L2VIC_INT_STATUSn) >> 2);
    } else if (offset >= L2VIC_INT_CLEARn && offset < L2VIC_SOFT_INTn) {
        /* INT_CLEARn is write-only, return 0 on read */
        value = 0;
    } else if (offset >= L2VIC_SOFT_INTn && offset < L2VIC_INT_PENDINGn) {
        value = 0;
    } else if (offset >= L2VIC_INT_PENDINGn &&
               offset < L2VIC_INT_PENDINGn + 0x80) {
        value = read_word(s->int_pending, (offset - L2VIC_INT_PENDINGn) >> 2);
    } else if (offset >= L2VIC_INT_GRPn_0 && offset < L2VIC_INT_GRPn_1) {
        value = read_word(s->int_group_n0, (offset - L2VIC_INT_GRPn_0) >> 2);
    } else if (offset >= L2VIC_INT_GRPn_1 && offset < L2VIC_INT_GRPn_2) {
        value = read_word(s->int_group_n1, (offset - L2VIC_INT_GRPn_1) >> 2);
    } else if (offset >= L2VIC_INT_GRPn_2 && offset < L2VIC_INT_GRPn_3) {
        value = read_word(s->int_group_n2, (offset - L2VIC_INT_GRPn_2) >> 2);
    } else if (offset >= L2VIC_INT_GRPn_3 && offset < L2VIC_INT_GRPn_3 + 0x80) {
        value = read_word(s->int_group_n3, (offset - L2VIC_INT_GRPn_3) >> 2);
    } else {
        value = 0;
        qemu_log_mask(LOG_GUEST_ERROR,
                      "L2VIC: %s: offset 0x%" HWADDR_PRIx "\n", __func__,
                      offset);
    }

    trace_l2vic_reg_read((unsigned)offset, value);

    return value;
}

static const MemoryRegionOps l2vic_ops = {
    .read = l2vic_read,
    .write = l2vic_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .valid.unaligned = false,
};

#define FASTL2VIC_ENABLE 0x0
#define FASTL2VIC_DISABLE 0x1
#define FASTL2VIC_INT 0x2

static void fastl2vic_write(void *opaque, hwaddr offset, uint64_t val,
                            unsigned size)
{
    if (offset == 0) {
        uint32_t cmd = (val >> 16) & 0x3;
        uint32_t irq = val & 0x3ff;
        uint32_t slice = (irq / 32) * 4;
        val = 1 << (irq % 32);

        if (cmd == FASTL2VIC_ENABLE) {
            l2vic_write(opaque, L2VIC_INT_ENABLE_SETn + slice, val, size);
        } else if (cmd == FASTL2VIC_DISABLE) {
            l2vic_write(opaque, L2VIC_INT_ENABLE_CLEARn + slice, val, size);
        } else if (cmd == FASTL2VIC_INT) {
            l2vic_write(opaque, L2VIC_SOFT_INTn + slice, val, size);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: invalid write cmd %" PRId32 "\n",
                          __func__, cmd);
        }
        return;
    }
    qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid write offset 0x%08" HWADDR_PRIx
            "\n", __func__, offset);
}

static const MemoryRegionOps fastl2vic_ops = {
    .write = fastl2vic_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .valid.unaligned = false,
};

/* L2VIC Interface Implementation */
static uint32_t l2vic_interface_read_vid_impl(L2VicInterface *iface,
                                               uint32_t group)
{
    L2VICState *s = L2VIC(iface);
    uint32_t result = 0;

    if (group == 0) {
        /* VID register: VID1 (bits 16-31), VID0 (bits 0-15) */
        result = deposit32(result, 0, 16, s->vid_group[0]);
        result = deposit32(result, 16, 16, s->vid_group[1]);
    } else if (group == 1) {
        /* VID1 register: VID3 (bits 16-31), VID2 (bits 0-15) */
        result = deposit32(result, 0, 16, s->vid_group[2]);
        result = deposit32(result, 16, 16, s->vid_group[3]);
    }
    return result;
}

static void l2vic_interface_update_vid_impl(L2VicInterface *iface,
                                            uint32_t group, uint32_t value)
{
    L2VICState *s = L2VIC(iface);

    QEMU_LOCK_GUARD(&s->active);

    if (group == 0) {
        /* VID register: unpack VID0 and VID1 */
        s->vid_group[0] = extract32(value, 0, 16);
        s->vid_group[1] = extract32(value, 16, 16);
    } else if (group == 1) {
        /* VID1 register: unpack VID2 and VID3 */
        s->vid_group[2] = extract32(value, 0, 16);
        s->vid_group[3] = extract32(value, 16, 16);
    }

    l2vic_update_all(s);
}

static void l2vic_interface_clear_interrupt_impl(L2VicInterface *iface)
{
    L2VICState *s = L2VIC(iface);

    QEMU_LOCK_GUARD(&s->active);
    /*
     * Only one interrupt can be active at a time (further delivery is gated by
     * vid_active()), so clear whichever status bit is currently set.
     */
    uint32_t irq = find_first_bit32(s->int_status, L2VIC_INTERRUPT_MAX);
    if (irq != L2VIC_INTERRUPT_MAX) {
        clear_bit32(irq, s->int_status);
    }
    l2vic_update_all(s);
}

static void l2vic_reset_hold(Object *obj, ResetType type G_GNUC_UNUSED)
{
    L2VICState *s = L2VIC(obj);
    memset(s->int_enable, 0, sizeof(s->int_enable));
    memset(s->int_pending, 0, sizeof(s->int_pending));
    memset(s->int_status, 0, sizeof(s->int_status));
    memset(s->int_type, 0, sizeof(s->int_type));
    memset(s->int_group_n0, 0, sizeof(s->int_group_n0));
    memset(s->int_group_n1, 0, sizeof(s->int_group_n1));
    memset(s->int_group_n2, 0, sizeof(s->int_group_n2));
    memset(s->int_group_n3, 0, sizeof(s->int_group_n3));
    memset(s->vid_group, 0, sizeof(s->vid_group));

    l2vic_update_all(s);
}

static void reset_irq_handler(void *opaque, int irq, int level)
{
    L2VICState *s = (L2VICState *)opaque;
    Object *obj = OBJECT(opaque);
    if (level) {
        l2vic_reset_hold(obj, RESET_TYPE_COLD);
    }
    l2vic_update_all(s);
}

static void l2vic_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    L2VICState *s = L2VIC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    int i;

    memory_region_init_io(&s->iomem, obj, &l2vic_ops, s, "l2vic", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    memory_region_init_io(&s->fast_iomem, obj, &fastl2vic_ops, s, "fast",
                          0x10000);
    sysbus_init_mmio(sbd, &s->fast_iomem);

    qdev_init_gpio_in(dev, l2vic_set_irq, L2VIC_INTERRUPT_MAX);
    qdev_init_gpio_in_named(dev, reset_irq_handler, "reset", 1);
    for (i = 0; i < 8; i++) {
        sysbus_init_irq(sbd, &s->irq[i]);
    }
    qemu_mutex_init(&s->active);
}

static const VMStateDescription vmstate_l2vic = {
    .name = "l2vic",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields =
        (VMStateField[]){
            VMSTATE_UINT32_ARRAY(vid_group, L2VICState, 4),
            VMSTATE_UINT32_ARRAY(int_enable, L2VICState, SLICE_MAX),
            VMSTATE_UINT32_ARRAY(int_type, L2VICState, SLICE_MAX),
            VMSTATE_UINT32_ARRAY(int_status, L2VICState, SLICE_MAX),
            VMSTATE_UINT32_ARRAY(int_pending, L2VICState, SLICE_MAX),
            VMSTATE_UINT32_ARRAY(int_group_n0, L2VICState, SLICE_MAX),
            VMSTATE_UINT32_ARRAY(int_group_n1, L2VICState, SLICE_MAX),
            VMSTATE_UINT32_ARRAY(int_group_n2, L2VICState, SLICE_MAX),
            VMSTATE_UINT32_ARRAY(int_group_n3, L2VICState, SLICE_MAX),
            VMSTATE_END_OF_LIST() }
};

static void l2vic_interface_class_init(ObjectClass *klass, const void *data)
{
    L2VicInterfaceClass *k = L2VIC_INTERFACE_CLASS(klass);

    k->read_vid = l2vic_interface_read_vid_impl;
    k->update_vid = l2vic_interface_update_vid_impl;
    k->clear_interrupt = l2vic_interface_clear_interrupt_impl;
}

static void l2vic_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->vmsd = &vmstate_l2vic;
    rc->phases.hold = l2vic_reset_hold;
}

static const TypeInfo l2vic_interface_info = {
    .name = TYPE_L2VIC_INTERFACE,
    .parent = TYPE_INTERFACE,
    .class_size = sizeof(L2VicInterfaceClass),
    .class_init = l2vic_interface_class_init,
};

static const TypeInfo l2vic_info = {
    .name = TYPE_L2VIC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(L2VICState),
    .instance_init = l2vic_init,
    .class_size = sizeof(L2VICClass),
    .class_init = l2vic_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_L2VIC_INTERFACE },
        { }
    },
};

static void l2vic_register_types(void)
{
    type_register_static(&l2vic_interface_info);
    type_register_static(&l2vic_info);
}

type_init(l2vic_register_types)
