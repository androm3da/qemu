# h2 test-suite progress — 2026-09-13

## Test setup

- QEMU source: `bcain/pcycle` at `32a0765103`, plus the working-tree changes
  recorded below.
- QEMU binary: `build_claude_hex/qemu-system-hexagon`.
- h2 source: `/home/brian/src/hexagon-hypervisor` on `bcain/qemu_boot` at
  `29e6b7af` (rebuilt on September 13 before the current focused results).
- h2 configuration: `ARCHV=68 TARGET=opt USE_PKW=0 NULL_ANGEL_TRAP=1`.
- machine: `V68N_1024`.
- runner: `/home/brian/src/hexagon-hypervisor/scripts/run_qemu.sh`.

The previous complete baseline in `h2_test_suite_aug17.md` was 122/139 passing:
63/63 STANDALONE, 7/12 MONITOR, and 52/64 BOOT.

## Test-to-fix map

| Test | Before | Current result | Required QEMU work |
|---|---|---|---|
| `libs/h2/vmtraps/test/test_pcycles` | Failed because PCYCLE was not implemented | **PASS** | Use translated packet cycles for PCYCLE, GPCYCLE, and UPCYCLE; v68 requires three performance cycles per packet. |
| `kernel/traps/cputime/test` | `pcycles not incrementing` in the August baseline; then `cputime/pcycles diverging` with the wall-clock implementation | FAIL: delay checks A/B pass; delay check C fails | Use one packet-derived source for PCYCLE and UPCYCLE and count three cycles per packet. The fresh h2 image exposes excessive core-cycle growth while thread 0 waits for thread 1; isolate interrupt/scheduling overhead in check C. |
| `libs/h2_compat/sleep/test` | `Bad delta` | RR FAIL: main observes `delta == 0`; MTTCG wakes child but reports about 34 ms instead of 5 ms | This test reads QTimer time, not PCYCLE. RR lets the main thread exhaust its busy-loop guard before the child runs; MTTCG exposes host-time scheduling latency. Timer reads are now marked as I/O so deterministic `-icount` no longer aborts, but RR fairness still prevents this test passing. |
| `kernel/event/intpool/test_h2` | Timing-counter failure in August baseline | FAIL: RR handles through roughly vector 52; MTTCG branch handles through roughly vector 57, then `time-out` | L2VIC tracing proves every interrupt sent before the timeout is delivered. The instruction-count timeout is consumed by multicore spin/semihost overhead; the local `bcain/mttcg` series improves progress but does not make this diagnostic-heavy test pass. |
| `kernel/time/timer/test_h2_multi` | Timing-counter failure in August baseline | RR TIMEOUT after two tasks start; MTTCG branch starts four tasks, then times out | Both results stop before a task programs its first timeout, so this is pre-QTimer guest locking/scheduling progress rather than a missed timer expiry. Evaluate the local MTTCG series together with current fixes. |
| `kernel/futex/futex/test/tests/simple_lock_unlock` | RR TIMEOUT after producer resumes the consumer | **PASS on the integrated MTTCG working tree** | Concurrent vCPU execution is required for the producer/consumer handoff. The complete MTTCG safety series is now ported; enabling MTTCG alone is unsafe. |
| `kernel/futex/futex/test/tests/find_match` | RR FAIL: `counter 0 bad` | MTTCG TIMEOUT after the first test thread starts | MTTCG alone regresses progress with the current h2 image; inspect LL/SC reservation and scheduler behavior before claiming this subset. |
| `kernel/futex/futex/test/tests/multi_wake` | RR FAIL: only 2 of 24 expected threads wake | MTTCG TIMEOUT after the producer requests 24 wakes | The producer reaches `h2_futex_wake`, but consumers do not finish. Investigate concurrent LL/SC/futex wake completion after the MTTCG prerequisite is integrated. |
| `kernel/futex/futex/test/tests/pi` | `FAIL t0 not spinning` | Same failure under MTTCG | Not fixed by concurrency alone; isolate PI scheduler-state observation and LL/SC ordering. |

## 2026-09-13: authoritative h2 rebuild and result correction

Initial focused runs accidentally used an h2 booter built on September 4. MMU
logging showed that image installed a 1 MiB device page (`...9010`), leaving
the L2VIC at `0xfc910000` and QTimer at `0xfc920000` outside the mapping and
backed by RAM. The current h2 source contains the 4 MiB mapping fix, but merely
relinking individual tests did not rebuild the installed booter.

The complete h2 tree was rebuilt from `29e6b7af`. All current results in the
table above use that image (the boot log reports `Build ID: 0x29e6b7af`). This
activates the modeled devices: `intpool` now services vectors 48 through 53
before timing out, rather than timing out before any worker activity. Results
from the stale image are retained below as development history, not as current
pass/fail evidence.

## 2026-09-13: unify architectural cycle-counter views

The branch initially backed the global PCYCLE registers with
`QEMU_CLOCK_VIRTUAL`, while the existing `HEX_SYS_READ_PCYCLES` path and h2
cputime accounting used QEMU's translated-packet counter. Consequently, two
back-to-back reads of UPCYCLE and PCYCLE diverged immediately.

Working-tree fix:

- route PCYCLE, GPCYCLE, and UPCYCLE through
  `hexagon_get_sys_pcycle_count()`;
- restore writes to PCYCLELO/PCYCLEHI through the same counter;
- count three performance cycles per translated packet for v68;
- retain a prospective virtual-clock contribution only for periods where all
  hardware threads are stopped, though the sleep test shows that MODECTL alone
  is not yet sufficient to detect the relevant idle interval.

Evidence:

```text
kernel/traps/cputime/test (before)
FAIL
cputime/pcycles diverging

kernel/traps/cputime/test (after unified source, before 3 cycles/packet)
delta 1 = 2097112,pcycles=768
FAIL
Unexpected delta based on delay (A)

kernel/traps/cputime/test (after 3 cycles/packet, stale h2 image)
delta 1 = 120,pcycles=768
delta 2 = 120
delta pcycles: 300b3d - 300000 == b3d
delta cputime: 48f
TEST PASSED

libs/h2/vmtraps/test/test_pcycles
INFO:  TEST PASSED
```

QEMU's newly added functional `test_pcycle` currently exits with status 3 after
this change. Its source was recovered from `qualcomm/qemu-hexagon-testing`:
the first subtests compare PCYCLE deltas with per-opcode values from
`cycle_estimates.h.inc`, including packet parallelism. A fixed three cycles per
packet satisfies h2's loop but is not a complete architectural cycle model.
The wall clock happened to satisfy different timing behavior; neither model is
yet sufficient for both suites. This working tree is therefore not ready to
commit.

## 2026-09-13: kick K0/TLB lock waiters

The post-check timeout seen with the stale h2 image matched an already known
round-robin TCG wake defect fixed by `hex-next` commit `54832ec286`. Under RR
TCG, `cpu_interrupt()` sees the shared host CPU thread as self and does not kick
it. An interrupt request can therefore remain pending on a halted K0 or TLB
lock waiter. Calling `qemu_cpu_kick()` after the unlock interrupt makes the
awakened h2 main thread run. With the fresh h2 image, the test gets past the
first two exact cycle checks but fails check C while one thread waits for a
second thread's delay loop. Thus the wake fix remains relevant, but the earlier
full PASS is superseded.

The experimental MODECTL-based idle wall-clock contribution was removed after
the fresh image made it cause immediate `cputime/pcycles diverging` failures.
Mixing host-time cycles into the packet-derived system count can change the
counter between paired reads. Idle-time advancement needed by `sleep` must be
implemented without contaminating running-thread accounting.

Further source inspection corrected the interpretation of `sleep`: it calls
`h2_vmtrap_timerop(GET_TIME)`, not a cycle-counter API. Under RR, the main
thread completes its `COUNT_MAX` polling loop before the sleeper is scheduled,
so it prints the untouched global `delta == 0`; QTimer tracing independently
shows the deadline firing. Under the clean MTTCG branch the sleeper wakes, but
the QTimer's host-time-backed counter includes about 34 ms of emulator
scheduling latency for a requested 5 ms interval.

Next task: isolate why `kernel/event/intpool/test_h2` consistently stops during
the multi-vector burst after successfully handling its initial interrupts.

## 2026-09-13: round-robin versus MTTCG isolation

L2VIC tracing for `kernel/event/intpool/test_h2` records one delivery for every
software interrupt the main thread manages to send. There is no pending vector
that QEMU silently loses: the test's low-priority thread exhausts its fixed
4-Mpacket timeout while the sender and worker are still making progress. This
is consistent with round-robin TCG letting a vCPU burn a complete slice while
spinning on a lock whose owner cannot run concurrently.

The local `bcain/mttcg` branch is directly relevant. Its final series is:

- `f50c85f8d9` explicit TCG memory barriers;
- `bc684fc45c` serialization of global synchronization packets;
- `4c6d19e7a7` LL/SC rework suitable for concurrent vCPUs;
- `31886b1ada` LL/SC reservation clearing at context boundaries;
- `844ef64b65` locking for the shared architectural TLB;
- `9364d5829c` publication of TLB writes before cross-vCPU flushes;
- `267c92d30b` user `icinva` translated-code invalidation;
- `6d71bd8c67` rebase fixes; and
- `fcea0e5bab` MTTCG enablement.

A clean detached build of that branch was tested against the same h2 image.
`intpool` advanced to approximately vector 57 rather than approximately 52,
and `timer/test_h2_multi` started four tasks rather than two. Neither completed,
so the series is a promising concurrency prerequisite, not yet a demonstrated
test fix. Running the current tree with MTTCG forced but without the safety
series crashed, confirming that the final one-line enablement must not be
applied alone.

QTimer/L2VIC tracing of `timer/test_h2_multi` showed only boot-time timer
initialization. No task reached its first compare programming before the
timeout, further separating this failure from QTimer deadline or interrupt
delivery semantics.

Fresh futex comparisons provide a stronger MTTCG result. With the authoritative
booter (`Build ID: 0x29e6b7af`), `simple_lock_unlock` changes from an RR timeout
to `TEST PASSED` on `bcain/mttcg`. This is the first newly passing h2 case found
from that series. `find_match` and `multi_wake` stall under MTTCG, while `pi`
still reports `t0 not spinning`, so the remaining futex cases need additional
work after the safety series is integrated.

An initial manual comparison accidentally selected the stale May booter at
`booter/booter` (`Build ID: 0x433a47d6`). Those results were discarded. The
authoritative comparisons use
`artifacts/v68/opt/build/booter/booter` and report `0x29e6b7af`.

The complete MTTCG safety series has now been ported into the current working
tree while preserving the PCYCLE, timer-read, interrupt-lock, and pause work:
explicit memory barriers, exclusive replay of global synchronization packets,
physical-address LL/SC reservations with cross-vCPU invalidation, reservation
clearing at exception/reset boundaries, shared architectural-TLB locking,
publication before cross-vCPU TLB flushes, and translated-code invalidation for
`icinva`. Both `qemu-system-hexagon` and `qemu-hexagon` build successfully with
MTTCG enabled.

The integrated binary reproduces `simple_lock_unlock` as `TEST PASSED` in under
one second using the authoritative `0x29e6b7af` booter, repeated three times
with three passes. `test_pcycles` also
continues to pass. A standalone 120-second run of `find_match` still stalls
after printing its first TID, and `pi` still reports `t0 not spinning`.

A whole-suite invocation on September 14 was not accepted as a fresh total:
h2 retained old `results.txt` files for tests it did not rebuild, including
September 4 futex passes. Forced individual tests remain the source of truth.
Forced runs confirm that monitor `error/test_debug` still fails at `do_debug`
and `popup/test_h2` still times out after `Hello!`.

## 2026-09-13: pause yields the system vCPU

The system-mode `J2_pause` behavior from local `hex-next-rebased` commit
`f498fed11f` now raises `EXCP_YIELD`; user-mode behavior remains the existing PC
advance. This is architecturally useful for cooperative scheduling, but fresh
runs did not improve `sleep` or `intpool`, so it is recorded as a prerequisite
rather than a test fix.

## 2026-09-13: make timer reads safe under icount

Trying deterministic execution with `-icount shift=0,align=off,sleep=off`
previously terminated during boot with `Bad icount read`. Hexagon translated
reads of TIMERLO/TIMERHI and their UTIMER aliases called into
`QEMU_CLOCK_VIRTUAL` without first ending the TB and enabling I/O for the
instruction.

The translator now calls `translator_io_start()` for scalar and pair reads of
these architectural timer registers. The Hexagon semantics generator passes
the active `DisasContext` into system-register read generation so generated
instructions receive the same handling. With this change, the complete h2
booter and `sleep` test run under `-icount` without the QEMU abort. The test
still fails because its main vCPU exhausts the polling guard before RR reaches
the child, so this is an icount correctness prerequisite rather than a claimed
h2 pass.

## Verification

- QEMU rebuilt successfully with `make -j26 qemu-system-hexagon`.
- After integrating MTTCG, both configured Hexagon binaries rebuild
  successfully (`qemu-system-hexagon` and `qemu-hexagon`).
- `scripts/checkpatch.pl` reports no issues for the QEMU source diff.
- `git diff --check` passes.
- `make check-qtest-hexagon -j26`: 11 passed, 2 skipped, 0 failed.
- Known failure: functional `test_systests.SysTestsStandaloneTests.test_pcycle`
  exits 3, as described above.
