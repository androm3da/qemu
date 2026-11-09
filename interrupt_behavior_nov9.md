# QEMU hexagon: pending interrupts re-wake threads in SSR.EX (k0lock waiters spin)

Draft for review against the Hexagon System Architecture spec, 2026-11-09.
Tree: `bcain/minivm` at 8e441f0b4c2 (QEMU 11.1.50), debug build
(`--enable-debug --enable-debug-tcg`), machine `qcs6490-cdsp`.

## Status of the claims

I could not decide from hexagon-sim whether this is a conformance bug, so I
am asking you to rule on it. Please check each item against the spec:

| # | Claim | Basis |
|---|-------|-------|
| A | A thread blocked on `k0lock` takes no interrupt while blocked. | Taken from QEMU's own `hex_is_qualified_for_int()` (`lock_pending`). **Not checked against the spec by me.** |
| B | A thread in SSR.EX takes no interrupt. | Same function (`!ssr_ex`). **Not checked against the spec by me.** |
| C | So a blocked, in-EX thread with a pending interrupt can make no progress until the lock is released. | Follows from A and B if they hold. |
| D | QEMU burns host time re-waking such threads (measured, below). | Measured. |

This may therefore be (1) an optimization gap, with no guest-visible
behavior wrong, (2) a conformance bug, or (3) me misreading what the spec says
about interrupts, EX and lock waiters. I think it is (1). I could not build a
guest test that tells the difference (see "What I could not show").

## Symptom

`minivm` (Hexagon Virtual Machine monitor, `roms/hexagonMVM`) boots Linux on
`qcs6490-cdsp` in 39-49 s with the one-line change below, and does not reach
the console in 300 s without it (0 successes in about 10 solo boots over
several variants of the monitor). H2 (`hexagon_loadlinux_qcs6490_cdsp`) boots
in 66-69 s on the *unchanged* QEMU, so the monitor matters, but H2 hits the
same condition (below).

## Mechanism

1. Hexagon guest threads that handle an event run in SSR.EX. minivm takes a
   global lock with `k0lock` at the top of every handler, so with several
   threads in handlers most of them are blocked on the lock, in EX.
2. `hex_k0_lock()` (`op_helper.c:1371`) rewinds the PC, sets
   `k0_lock_state = HEX_LOCK_WAITING` and raises `CPU_INTERRUPT_HALT`.
3. Any interrupt pending in IPEND (the L2VIC line, an `swi` wake-up) raises
   `CPU_INTERRUPT_SWI` on every running thread (`hex_interrupt_update()`).
   `hexagon_cpu_has_work()` (`cpu.c:436`) then reports work, so the halted
   waiter wakes.
4. `hex_check_interrupts()` finds the interrupt pending but not qualified
   (`lock_pending`, or `ssr_ex`), and returns false.
5. Because `ssr_ex` is set, the block at `hex_interrupts.c:459`

   ```c
   if (!int_handled && !ssr_ex) {
       restore_state(env, int_handled);   /* cpu_reset_interrupt(HARD|SWI); re-HALT if waiting */
   }
   ```

   is skipped. `CPU_INTERRUPT_SWI` stays set, so the thread is not halted
   again by `restore_state`. It re-executes `k0lock`, goes WAITING, halts
   and is woken at once by the still-set request. This repeats indefinitely.
6. Each iteration takes the BQL (`BQL_LOCK_GUARD()` in
   `hex_check_interrupts` and `hex_k0_lock`). With N waiters this starves the
   lock owner, which also needs the BQL, so the lock hand-over slows to a crawl.

The comment above line 459 gives the reason for `!ssr_ex`: when the cause is EX,
the request is kept, because the thread may take the interrupt after EX clears.
That is sound only if request clearing is safe whenever EX clears. See below.

## Evidence

Counted with `-d int` over 25 s of boot (lines matching `found int`):

| Guest | `found int` per 25 s | Boots? |
|-------|----------------------|--------|
| minivm, unchanged QEMU | 3,018,151 | no (300 s) |
| H2, unchanged QEMU | 3.17 M | yes, ~66-69 s |

The log lines of the flood are of this shape
(`hex_check_interrupts ... int N not handled, GIE 1, iad 0, SSR:IE 1, SSR:EX: 1, imask bit 0`).
Hosts: H2 and minivm differ only in how well they tolerate it. `perf` on the
host showed bql_lock_impl at 24% (minivm) vs 6% (H2), and `cpu_handle_interrupt`
20% vs 8%.

Boot times, `qcs6490-cdsp`, vmlinux and initrd from `~/hexagon-boot-test/qcs6490-cdsp-dl`,
runs sequential (parallel runs were misleading and I discarded them):

| QEMU | minivm (original) | minivm (SYSCFG poll removed, simple ack) |
|------|-------------------|-------------------------------------------|
| unchanged | no boot in 300 s | no boot in 300 s |
| line 459 changed | 46-49 s (3 runs) | 39-41 s (3 runs) |

The `v85-sm8975` minivm (`468adf0`) also does not boot on unchanged QEMU.

## Proposed change

```diff
-    if (!int_handled && !ssr_ex) {
+    if (!int_handled) {
         restore_state(env, int_handled);
```

Why I think it is safe, and where to check me:

- Clearing `CPU_INTERRUPT_HARD|SWI` loses nothing while IPEND keeps the
  pending bit. `hex_interrupt_update()` re-raises the request when something
  changes. The relevant call sites:
  - SSR writes that leave EX or set IE: `cpu_helper.c:227` (`hexagon_modify_ssr`).
  - `k0unlock`: `op_helper.c:1485`.
  - `tlbunlock`: `hex_mmu.c:296`.
  - `ciad`: `op_helper.c:1724`.
  - Thread enable/disable (`start`, `stop`): the two sites with
    `thread_enabled_mask` around `op_helper.c:2130`.
  - `HELPER(pending_interrupt)`: `op_helper.c:2140`.
- Also confirmed by reading the code: `iassignw` ends in
  `hex_interrupt_update()` (`op_helper.c:1790`), and writes to IMASK, IAD,
  IPEND, IPENDAD and STID call `HELPER(pending_interrupt)` (`translate.c:1074-1081`).
- **Please check:** `rte` (`system.idef:67`, `fCLEAR_RTE_EX()`). I did not
  trace that macro down to `hexagon_modify_ssr`, and `hexagon_vmrte`
  (`hexswi.c`) does call it. If `rte` does not re-raise the request, dropping
  `!ssr_ex` could delay an interrupt after EX is left. I believe
  `func-hexagon-thorough` stayed green with the change (earlier session,
  2026-11-04), but I have not re-run it in this tree.
- If those paths are all covered, the change only removes wakes that cannot
  make progress.

## What I could not show

I tried to write a guest test in `hexagon-arch-tests` (`test_threads.rs`) that
shows a gap between hexagon-sim and QEMU, and failed. The shape: thread 1 sets
`SSR.EX|IE`, raises an `swi` on itself, executes `wait`, and thread 0 checks
that thread 1 stays in wait mode until `resume`. It failed the same way on
all three:

- hexagon-sim 19.0.04 (`--mv81`): thread 1 left `wait` without a `resume`,
- QEMU unchanged,
- QEMU with line 459 changed.

I reverted the test. Two readings, and I do not know which is right:

1. On hardware, `wait` ends when an interrupt is pending even if SSR.EX
   blocks it, and my expectation (C) is wrong for `wait`.
2. My test setup is wrong (for instance, setting EX by writing SSR in monitor
   mode might not be a state the simulator models like a taken exception).

This matters for A-C: if a pending interrupt wakes a waiting thread
irrespective of EX, the spec's wake rule may be "any pending, unmasked
interrupt", and the QEMU HALT/wake model for `k0lock` would need an
independent check on whether a lock waiter should wake at all.

The k0lock spin itself is not guest-visible. The thread re-executes `k0lock`
and re-blocks without running guest instructions, so no functional test
distinguishes the two builds except by elapsed time, which would be flaky.

## Questions for the spec review

1. Is a thread blocked on `k0lock` interruptible? (QEMU assumes not.) Does
   the answer differ for SSR.EX = 0 and 1?
2. Does a pending interrupt that SSR.EX blocks end a `wait`? hexagon-sim
   behaves as if it does.
3. Is anything in the spec observable about re-delivery after EX clears
   (e.g. ordering between `rte` and the next interrupt) that dropping `!ssr_ex`
   could change?
4. If the answer to 1 is "yes, a lock waiter is interruptible", then
   `lock_pending` in `hex_is_qualified_for_int()` is itself a conformance
   bug, and the right fix is different from the one proposed.

## Reproduction

```
cd build_claude_hex && make -j
# unchanged QEMU: no console in 300 s
qemu-system-hexagon -M qcs6490-cdsp -bios <minivm ELF> \
  -kernel ~/hexagon-boot-test/qcs6490-cdsp-dl/vmlinux \
  -initrd ~/hexagon-boot-test/qcs6490-cdsp-dl/initrd \
  -append 'console=ttyAMA0 mem=240M lpj=89124080 rdinit=/init' \
  -display none -serial stdio -monitor none
# watch for 'Please press Enter'. To see the flood add: -d int -D /tmp/int.log
grep -c 'found int' /tmp/int.log     # millions within seconds
```

minivm ELF: `roms/hexagonMVM` (`make minivm`; Rust nightly pinned in
`rust-toolchain.toml`, needs `hexagon-unknown-none-elf-ld.lld` on PATH).
The prebuilt `pc-bios/hexagon_minivm_qcs6490_cdsp` in this series shows the
same behavior.

## Things I ruled out (so you need not)

- minivm acknowledging (`ciad`) before taking its lock, as H2 does: no effect.
- Removing minivm's `SYSCFG.K0LOCK` poll: no effect on the unchanged QEMU
  (it was costly on its own, since each poll takes the BQL, but not the cause).
- Letting every thread take the L2VIC interrupt, and a lock-free
  `vmgetie`/`vmsetie` prototype: no reliable effect on the unchanged QEMU.
  Early "passes" were artifacts of running boots in parallel.
