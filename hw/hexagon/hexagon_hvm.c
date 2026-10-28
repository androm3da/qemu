/*
 * Hexagon Virtual Machine (HVM) monitor state
 *
 * The virtual interrupt controller implements the HVM interface.  A per-CPU
 * interrupt is taken when it is pending and
 * enabled; it has no local enable.  A shared one must also be locally
 * enabled on the CPU that takes it, and starts locally disabled
 * everywhere.  Taking an interrupt clears its enable, and the guest sets
 * that again when it has handled it.  Posting only makes an interrupt
 * pending.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/host-utils.h"
#include "qemu/main-loop.h"
#include "hw/hexagon/hexagon_hvm.h"
#include "hw/core/cpu.h"
#include "exec/cpu-interrupt.h"
#include "hw/core/irq.h"
#include "hw/core/qdev.h"
#include "migration/vmstate.h"
#include "system/runstate.h"

/* The timer behind vmtimerop: a 19.2 MHz counter, reported in 52 ns units. */
#define HVM_TIMER_HZ        19200000ULL
#define HVM_NS_PER_TICK     52ULL
/* Shortest timeout vmtimerop(deltatimeout) arms, in ticks. */
#define HVM_TICK_GRANULARITY 4

#define HVM_FOREVER         (~0ULL)

/* The shared interrupt that device line @n arrives on. */
#define LINE_TO_IDX(n)  ((n) + HVM_DEVICE_INT_BASE - HVM_PERCPU_INTS)

static inline bool bm_test(const uint32_t *bm, unsigned idx)
{
    return bm[idx / 32] & (1u << (idx % 32));
}

static inline void bm_set(uint32_t *bm, unsigned idx)
{
    bm[idx / 32] |= 1u << (idx % 32);
}

static inline void bm_clear(uint32_t *bm, unsigned idx)
{
    bm[idx / 32] &= ~(1u << (idx % 32));
}

static void hexagon_hvm_kick(HexagonHVMState *s, unsigned cpu)
{
    if (cpu < HVM_MAX_VCPUS && s->cpus[cpu] && (s->started & BIT(cpu))) {
        cpu_interrupt(s->cpus[cpu], CPU_INTERRUPT_HARD);
    }
}

/* Per-CPU interrupts that can be taken now. */
static uint16_t cpuint_ready(HexagonHVMState *s, unsigned cpu)
{
    return s->cpuint_pending[cpu] & s->cpuint_enabled[cpu];
}

static uint32_t shared_ready(HexagonHVMState *s, unsigned cpu, unsigned w)
{
    return s->pending[w] & s->enable[w] & s->percpu_mask[cpu][w];
}

int32_t hexagon_hvm_int_peek(HexagonHVMState *s, unsigned cpu)
{
    uint16_t ready = cpuint_ready(s, cpu);

    if (ready) {
        return ctz32(ready);
    }
    for (unsigned w = 0; w < HVM_SHARED_INTS / 32; w++) {
        uint32_t r = shared_ready(s, cpu, w);

        if (r) {
            return HVM_PERCPU_INTS + w * 32 + ctz32(r);
        }
    }
    return -1;
}

int32_t hexagon_hvm_int_get(HexagonHVMState *s, unsigned cpu)
{
    uint16_t ready = cpuint_ready(s, cpu);

    if (ready) {
        unsigned bit = ctz32(ready);

        s->cpuint_pending[cpu] &= ~BIT(bit);
        s->cpuint_enabled[cpu] &= ~BIT(bit);
        return bit;
    }
    for (unsigned w = 0; w < HVM_SHARED_INTS / 32; w++) {
        uint32_t r = shared_ready(s, cpu, w);

        if (r) {
            unsigned bit = ctz32(r);

            s->pending[w] &= ~BIT(bit);
            s->enable[w] &= ~BIT(bit);
            return HVM_PERCPU_INTS + w * 32 + bit;
        }
    }
    return -1;
}

/* Tell @cpu about shared interrupt @idx if it accepts it. */
static bool shint_deliver_cpu(HexagonHVMState *s, unsigned cpu, unsigned idx)
{
    if (!(s->started & BIT(cpu)) || !bm_test(s->percpu_mask[cpu], idx)) {
        return false;
    }
    hexagon_hvm_kick(s, cpu);
    return true;
}

/*
 * Tell every virtual CPU that accepts shared interrupt @idx.  Whether one can
 * take it now depends on that CPU's interrupt enable.  Notify every eligible
 * CPU so an enabled CPU can take the interrupt immediately.
 */
static void shint_deliver(HexagonHVMState *s, unsigned idx)
{
    for (unsigned cpu = 0; cpu < HVM_MAX_VCPUS; cpu++) {
        shint_deliver_cpu(s, cpu, idx);
    }
}

static void shint_post(HexagonHVMState *s, unsigned idx)
{
    if (bm_test(s->pending, idx)) {
        return;
    }
    bm_set(s->pending, idx);
    if (bm_test(s->enable, idx)) {
        shint_deliver(s, idx);
    }
}

static void cpuint_post(HexagonHVMState *s, unsigned cpu, unsigned intno)
{
    s->cpuint_pending[cpu] |= BIT(intno);
    if (cpuint_ready(s, cpu)) {
        hexagon_hvm_kick(s, cpu);
    }
}

/*
 * Rearm a device line after the guest handles or enables it.  Repost an
 * asserted line immediately.
 */
static void line_rearm(HexagonHVMState *s, unsigned idx)
{
    bm_set(s->armed, idx);
    if (bm_test(s->level, idx)) {
        bm_clear(s->armed, idx);
        shint_post(s, idx);
    }
}

static void hvm_set_irq(void *opaque, int line, int level)
{
    HexagonHVMState *s = opaque;
    unsigned idx = LINE_TO_IDX(line);

    if (level) {
        bm_set(s->level, idx);
        /*
         * A line remains disarmed until the guest handles and reenables it.
         * Do not repost a level that remains asserted in the interim.
         */
        if (bm_test(s->armed, idx)) {
            bm_clear(s->armed, idx);
            shint_post(s, idx);
        }
    } else {
        bm_clear(s->level, idx);
    }
}

static int32_t post(HexagonHVMState *s, unsigned target, unsigned intno)
{
    if (intno < HVM_PERCPU_INTS) {
        cpuint_post(s, target, intno);
    } else if (intno < HVM_PERCPU_INTS + HVM_SHARED_INTS) {
        shint_post(s, intno - HVM_PERCPU_INTS);
    } else {
        return -1;
    }
    return 0;
}

/* The virtual CPU a VM-relative CPU id names, or -1. */
static int cpu_of(HexagonHVMState *s, uint32_t id)
{
    unsigned vmidx = extract32(id, 26, 6);
    unsigned cpuidx = extract32(id, 10, 16);

    if (vmidx != HVM_VMIDX || cpuidx >= HVM_MAX_VCPUS ||
        !(s->started & BIT(cpuidx))) {
        return -1;
    }
    return cpuidx;
}

static int32_t cpuint_op(HexagonHVMState *s, unsigned cpu, uint32_t op,
                         unsigned intno)
{
    uint16_t bit = BIT(intno);

    switch (op) {
    case HVM_INTOP_GLOBEN:
        s->cpuint_enabled[cpu] |= bit;
        break;
    case HVM_INTOP_GLOBDIS:
        s->cpuint_enabled[cpu] &= ~bit;
        break;
    case HVM_INTOP_CLEAR:
        s->cpuint_pending[cpu] &= ~bit;
        break;
    case HVM_INTOP_STATUS:
        return ((s->cpuint_pending[cpu] >> intno) & 1) |
               (((s->cpuint_enabled[cpu] >> intno) & 1) << 2);
    default:
        /* A per-CPU interrupt has no local enable or affinity. */
        return -1;
    }
    /* Enabling may have made a pending interrupt deliverable. */
    if (cpuint_ready(s, cpu)) {
        hexagon_hvm_kick(s, cpu);
    }
    return 0;
}

static int32_t shint_op(HexagonHVMState *s, unsigned cpu, uint32_t op,
                        unsigned intno, uint32_t arg)
{
    unsigned idx = intno - HVM_PERCPU_INTS;

    switch (op) {
    case HVM_INTOP_GLOBEN:
        if (!bm_test(s->enable, idx)) {
            bm_set(s->enable, idx);
            if (intno >= HVM_DEVICE_INT_BASE) {
                line_rearm(s, idx);
            }
            if (bm_test(s->pending, idx)) {
                shint_deliver(s, idx);
            }
        }
        break;
    case HVM_INTOP_GLOBDIS:
        bm_clear(s->enable, idx);
        break;
    case HVM_INTOP_LOCEN:
        if (!bm_test(s->percpu_mask[cpu], idx)) {
            bm_set(s->percpu_mask[cpu], idx);
            if (bm_test(s->enable, idx) && bm_test(s->pending, idx)) {
                shint_deliver_cpu(s, cpu, idx);
            }
        }
        break;
    case HVM_INTOP_LOCDIS:
        bm_clear(s->percpu_mask[cpu], idx);
        break;
    case HVM_INTOP_AFFINITY:
        if (arg >= HVM_MAX_VCPUS) {
            return -1;
        }
        for (unsigned c = 0; c < HVM_MAX_VCPUS; c++) {
            bm_clear(s->percpu_mask[c], idx);
        }
        bm_set(s->percpu_mask[arg], idx);
        if (bm_test(s->enable, idx) && bm_test(s->pending, idx)) {
            shint_deliver_cpu(s, arg, idx);
        }
        break;
    case HVM_INTOP_CLEAR:
        bm_clear(s->pending, idx);
        if (bm_test(s->enable, idx) && intno >= HVM_DEVICE_INT_BASE) {
            line_rearm(s, idx);
        }
        break;
    case HVM_INTOP_STATUS:
        return bm_test(s->pending, idx) |
               (bm_test(s->percpu_mask[cpu], idx) << 1) |
               (bm_test(s->enable, idx) << 2);
    default:
        return -1;
    }
    return 0;
}

int32_t hexagon_hvm_intop(HexagonHVMState *s, unsigned cpu, uint32_t op,
                          uint32_t intno, uint32_t arg)
{
    switch (op) {
    case HVM_INTOP_NOP:
        return 0;
    case HVM_INTOP_GET:
        return hexagon_hvm_int_get(s, cpu);
    case HVM_INTOP_PEEK:
        return hexagon_hvm_int_peek(s, cpu);
    case HVM_INTOP_POST: {
        /* A zero id means the caller. */
        int target = arg == 0 ? (int)cpu : cpu_of(s, arg);

        return target < 0 ? -1 : post(s, target, intno);
    }
    case HVM_INTOP_GLOBEN:
    case HVM_INTOP_GLOBDIS:
    case HVM_INTOP_LOCEN:
    case HVM_INTOP_LOCDIS:
    case HVM_INTOP_AFFINITY:
    case HVM_INTOP_STATUS:
    case HVM_INTOP_CLEAR:
        break;
    default:
        return -1;
    }

    if (intno < HVM_PERCPU_INTS) {
        return cpuint_op(s, cpu, op, intno);
    } else if (intno < HVM_PERCPU_INTS + HVM_SHARED_INTS) {
        return shint_op(s, cpu, op, intno, arg);
    }
    return -1;
}

/* Account the run that is in progress to @cpu, up to @now. */
static void cpu_time_fold(HexagonHVMState *s, unsigned cpu, int64_t now)
{
    if (s->run_since[cpu] >= 0) {
        s->run_ns[cpu] += now - s->run_since[cpu];
        s->run_since[cpu] = now;
    }
}

/* @cpu starts or stops running as far as vmgettime is concerned. */
static void cpu_time_run(HexagonHVMState *s, unsigned cpu, bool running)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    cpu_time_fold(s, cpu, now);
    s->run_since[cpu] = running ? now : -1;
}

void hexagon_hvm_set_waiting(HexagonHVMState *s, unsigned cpu, bool waiting)
{
    cpu_time_run(s, cpu, !waiting);
    if (waiting) {
        s->waiting |= BIT(cpu);
    } else {
        s->waiting &= ~BIT(cpu);
    }
}

/* ---- timers ---- */

static uint64_t hvm_now_ticks(void)
{
    return muldiv64(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL), HVM_TIMER_HZ,
                    NANOSECONDS_PER_SECOND);
}

static void hvm_rearm_timer(HexagonHVMState *s)
{
    uint64_t next = HVM_FOREVER;

    for (unsigned cpu = 0; cpu < HVM_MAX_VCPUS; cpu++) {
        if (s->timeout[cpu] && s->timeout[cpu] < next) {
            next = s->timeout[cpu];
        }
    }
    if (next == HVM_FOREVER) {
        timer_del(s->timer);
        return;
    }
    /*
     * Round up so the timer never fires before the timeout.  Values outside
     * the clock's range do not arm the timer.
     */
    timer_mod_ns(s->timer,
                 MIN(muldiv64(next, NANOSECONDS_PER_SECOND, HVM_TIMER_HZ),
                     (uint64_t)INT64_MAX - 1) + 1);
}

/*
 * Set @cpu's timeout to an absolute tick; 0 cancels it.  A timeout that has
 * already passed is dropped rather than delivered.
 */
static uint64_t hvm_set_timeout_tick(HexagonHVMState *s, unsigned cpu,
                                     uint64_t tick, uint64_t now)
{
    if (tick <= now) {
        tick = 0;
    }
    s->timeout[cpu] = tick;
    hvm_rearm_timer(s);
    return tick;
}

static void hvm_timer_cb(void *opaque)
{
    HexagonHVMState *s = opaque;
    uint64_t now = hvm_now_ticks() + HVM_TICK_GRANULARITY;

    for (unsigned cpu = 0; cpu < HVM_MAX_VCPUS; cpu++) {
        if (s->timeout[cpu] && s->timeout[cpu] <= now) {
            s->timeout[cpu] = 0;
            cpuint_post(s, cpu, HVM_TIMER_INT);
        }
    }
    hvm_rearm_timer(s);
}

uint64_t hexagon_hvm_timerop(HexagonHVMState *s, unsigned cpu, uint32_t op,
                             uint64_t arg)
{
    uint64_t now = hvm_now_ticks();
    uint64_t tick;

    switch (op) {
    case HVM_TIMEROP_GETFREQ:
        return HVM_TIMER_HZ * HVM_NS_PER_TICK;
    case HVM_TIMEROP_GETRES:
        return HVM_NS_PER_TICK;
    case HVM_TIMEROP_GETTIME:
        return now * HVM_NS_PER_TICK;
    case HVM_TIMEROP_GETTIMEOUT:
        return s->timeout[cpu] * HVM_NS_PER_TICK;
    case HVM_TIMEROP_SETTIMEOUT:
        tick = arg == HVM_FOREVER ? 0 : arg / HVM_NS_PER_TICK;
        return hvm_set_timeout_tick(s, cpu, tick, now) * HVM_NS_PER_TICK;
    case HVM_TIMEROP_DELTATIMEOUT:
        tick = arg == HVM_FOREVER ? 0 :
               now + MAX(arg / HVM_NS_PER_TICK, HVM_TICK_GRANULARITY);
        return hvm_set_timeout_tick(s, cpu, tick, now) * HVM_NS_PER_TICK;
    default:
        return HVM_FOREVER;
    }
}

uint64_t hexagon_hvm_gettime(HexagonHVMState *s, unsigned cpu,
                             uint64_t *pcycles)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    cpu_time_fold(s, cpu, now);
    *pcycles = now;
    return s->run_ns[cpu];
}

void hexagon_hvm_settime(HexagonHVMState *s, unsigned cpu, uint64_t time)
{
    cpu_time_fold(s, cpu, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
    s->run_ns[cpu] = time;
}

/* ---- virtual CPUs ---- */

void hexagon_hvm_add_cpu(HexagonHVMState *s, CPUState *cs, unsigned idx)
{
    g_assert(idx < HVM_MAX_VCPUS);
    s->cpus[idx] = cs;
    /* The boot CPU is running already; the guest starts the others. */
    if (idx == 0) {
        s->started |= BIT(0);
        s->num_cpus = 1;
        cpu_time_run(s, 0, true);
    }
}

int hexagon_hvm_alloc_cpu(HexagonHVMState *s)
{
    for (unsigned cpu = 0; cpu < HVM_MAX_VCPUS; cpu++) {
        if (s->cpus[cpu] && !(s->started & BIT(cpu))) {
            s->started |= BIT(cpu);
            s->num_cpus++;
            s->cpuint_pending[cpu] = 0;
            s->cpuint_enabled[cpu] = 0;
            s->timeout[cpu] = 0;
            s->run_ns[cpu] = 0;
            cpu_time_run(s, cpu, true);
            /* Shared interrupts start locally disabled on a new CPU. */
            memset(s->percpu_mask[cpu], 0, sizeof(s->percpu_mask[cpu]));
            return cpu;
        }
    }
    return -1;
}

void hexagon_hvm_cpu_stopped(HexagonHVMState *s, unsigned cpu,
                             uint32_t status)
{
    uint32_t reason = status <= HVM_STOP_MACHINECHECK ?
                      status : HVM_STOP_MACHINECHECK;

    cpu_time_run(s, cpu, false);
    s->started &= ~BIT(cpu);
    s->waiting &= ~BIT(cpu);
    s->timeout[cpu] = 0;
    hvm_rearm_timer(s);
    s->stop_reason = MAX(s->stop_reason, reason);
    s->num_cpus--;
    if (s->num_cpus) {
        return;
    }
    switch (s->stop_reason) {
    case HVM_STOP_RESTART:
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        break;
    case HVM_STOP_MACHINECHECK:
        qemu_system_shutdown_request_with_code(SHUTDOWN_CAUSE_GUEST_PANIC, 1);
        break;
    default:
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
        break;
    }
}

uint32_t hexagon_hvm_getinfo(HexagonHVMState *s, uint32_t code)
{
    return code < HVM_NUM_INFO ? s->info[code] : ~0u;
}

/* ---- QOM ---- */

static void hexagon_hvm_reset_hold(Object *obj, ResetType type)
{
    HexagonHVMState *s = HEXAGON_HVM(obj);

    memset(s->cpuint_pending, 0, sizeof(s->cpuint_pending));
    memset(s->cpuint_enabled, 0, sizeof(s->cpuint_enabled));
    memset(s->pending, 0, sizeof(s->pending));
    memset(s->enable, 0, sizeof(s->enable));
    /*
     * Device lines start disarmed.  Preserve their input levels so a line
     * asserted across reset can be reposted when the guest enables it.
     */
    memset(s->armed, 0, sizeof(s->armed));
    memset(s->percpu_mask, 0, sizeof(s->percpu_mask));
    memset(s->timeout, 0, sizeof(s->timeout));
    timer_del(s->timer);
    s->waiting = 0;
    s->stop_reason = HVM_STOP_NONE;
    s->started = s->cpus[0] ? BIT(0) : 0;
    s->num_cpus = s->started ? 1 : 0;
    for (unsigned cpu = 0; cpu < HVM_MAX_VCPUS; cpu++) {
        s->run_ns[cpu] = 0;
        s->run_since[cpu] = -1;
    }
    if (s->started) {
        cpu_time_run(s, 0, true);
    }
}

static void hexagon_hvm_realize(DeviceState *dev, Error **errp)
{
    HexagonHVMState *s = HEXAGON_HVM(dev);

    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, hvm_timer_cb, s);
    qdev_init_gpio_in(dev, hvm_set_irq, HVM_NUM_LINES);
}

static const VMStateDescription vmstate_hexagon_hvm = {
    .name = TYPE_HEXAGON_HVM,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(started, HexagonHVMState),
        VMSTATE_UINT32(waiting, HexagonHVMState),
        VMSTATE_UINT32(stop_reason, HexagonHVMState),
        VMSTATE_UINT32(num_cpus, HexagonHVMState),
        VMSTATE_UINT16_ARRAY(cpuint_pending, HexagonHVMState, HVM_MAX_VCPUS),
        VMSTATE_UINT16_ARRAY(cpuint_enabled, HexagonHVMState, HVM_MAX_VCPUS),
        VMSTATE_UINT32_ARRAY(pending, HexagonHVMState, HVM_SHARED_INTS / 32),
        VMSTATE_UINT32_ARRAY(enable, HexagonHVMState, HVM_SHARED_INTS / 32),
        VMSTATE_UINT32_ARRAY(armed, HexagonHVMState, HVM_SHARED_INTS / 32),
        VMSTATE_UINT32_ARRAY(level, HexagonHVMState, HVM_SHARED_INTS / 32),
        VMSTATE_UINT32_2DARRAY(percpu_mask, HexagonHVMState, HVM_MAX_VCPUS,
                               HVM_SHARED_INTS / 32),
        VMSTATE_UINT64_ARRAY(timeout, HexagonHVMState, HVM_MAX_VCPUS),
        VMSTATE_UINT64_ARRAY(run_ns, HexagonHVMState, HVM_MAX_VCPUS),
        VMSTATE_INT64_ARRAY(run_since, HexagonHVMState, HVM_MAX_VCPUS),
        VMSTATE_TIMER_PTR(timer, HexagonHVMState),
        VMSTATE_END_OF_LIST()
    },
};

static void hexagon_hvm_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->vmsd = &vmstate_hexagon_hvm;
    dc->realize = hexagon_hvm_realize;
    dc->user_creatable = false;
    rc->phases.hold = hexagon_hvm_reset_hold;
}

static const TypeInfo hexagon_hvm_info = {
    .name = TYPE_HEXAGON_HVM,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(HexagonHVMState),
    .class_init = hexagon_hvm_class_init,
};

static void hexagon_hvm_register_types(void)
{
    type_register_static(&hexagon_hvm_info);
}

type_init(hexagon_hvm_register_types)
