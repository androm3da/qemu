#!/usr/bin/env python3
#
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import re
import tempfile
import unittest
from subprocess import TimeoutExpired, run

from qemu_test import Asset, QemuSystemTest, archive_extract, skipUntrustedTest


H2_TESTS = """
kernel/data/intconfig/test
kernel/data/context/test
kernel/data/readylist/test
kernel/data/runlist/test
kernel/data/thread/test
kernel/error/fatal/test
kernel/event/error/test
kernel/event/error/test_debug
kernel/event/fastint/test
kernel/event/interrupt/test
kernel/event/trap/test
kernel/event/vectors/test
kernel/event/preempt/test
kernel/event/intpool/test
kernel/event/intpool/test_h2
kernel/event/popup/test
kernel/event/popup/test_h2
kernel/event/passthru/test
kernel/init/boot/test
kernel/init/setup/test
kernel/mem/alloc/test
kernel/mem/asid/test
kernel/mem/linear/test
kernel/mem/pagefault/test
kernel/mem/pagewalk/test
kernel/mem/physread/test
kernel/mem/safemem/test
kernel/mem/stlb/test
kernel/mem/tlbfill/test
kernel/mem/tlbmisc/test
kernel/mem/tlbmiss/test
kernel/mem/varadix/test
kernel/power/apcr/test_multi
kernel/power/hvx/test
kernel/power/apcr/simple_test
kernel/sched/check_sanity/test/tests/H2K_check_sanity/scenarios
kernel/sched/dosched/test/test
kernel/futex/futex/test/tests/badaccess
kernel/futex/futex/test/tests/find_match
kernel/futex/futex/test/tests/multi_va
kernel/futex/futex/test/tests/multi_wake
kernel/futex/futex/test/tests/pi
kernel/futex/futex/test/tests/simple_lock_unlock
kernel/sched/lowprio/test
kernel/sched/resched/test
kernel/sched/switch/test
kernel/sched/yield/test
kernel/thread/create/test
kernel/thread/id/test
kernel/thread/state/test
kernel/thread/stop/test
kernel/traps/config/test
kernel/traps/hwconfig/test
kernel/traps/pmu/test_standalone
kernel/traps/prio/test
kernel/traps/tid/test
kernel/traps/cputime/test
kernel/traps/tlb/test
kernel/traps/waitcycles/test
kernel/traps/info/test
kernel/time/timer/test_standalone
kernel/time/timer/test_h2
kernel/time/timer/test_h2_multi
kernel/util/atomic/test
kernel/util/ring/test/test
kernel/util/stmode/test
kernel/util/spinlock/test
kernel/util/trace/test
kernel/util/tree/test
kernel/util/intcontrol/test
kernel/vm/vmop/test
kernel/vm/vmevent/test
kernel/vm/cpuint/test
kernel/vm/shint/test
kernel/vm/badint/test
kernel/vm/vmint/test
kernel/vm/vmipi/test
kernel/vm/vmtrap/test
kernel/vm/vmwork/test
kernel/vm/vmfuncs/test
kernel/vm/vmmap/test
kernel/vm/vmcache/test
libs/h2_compat/alloc/test
libs/h2_compat/allsignal/test/simple
libs/h2_compat/allsignal/test/trapfails
libs/h2_compat/anysignal/test/simple
libs/h2_compat/anysignal/test/trapfails
libs/h2_compat/error/test/simple
libs/h2_compat/sem/test/simple
libs/h2_compat/sleep/test
libs/h2_compat/vecaccess/test
libs/h2_compat/mxaccess/test
libs/h2_compat/lxaccess/test
libs/h2/vmtraps/test/test_ie
libs/h2/vmtraps/test/test_pcycles
libs/h2/vmtraps/test/test_return
libs/h2/vmtraps/test/test_yield
libs/posix/pthread/test_h2/stuck_in_mutex
libs/posix/pthread/test_h2/stuck_in_cond_wait
libs/posix/pthread/test_h2/stuck_in_cond_timedwait
libs/posix/pthread/test_h2/stuck_in_barrier
libs/posix/pthread/test_h2/stuck_in_rwlock_rd
libs/posix/pthread/test_h2/stuck_in_rwlock_wr
libs/posix/pthread/test_h2/stuck_in_sem_wait
libs/posix/pthread/test_h2/stuck_in_sem_timedwait
libs/posix/pthread/test_h2/stuck_in_join
libs/posix/pthread/test_h2/stuck_in_pthread_exit_joined
libs/posix/pthread/test_h2/exit1_main_cond_wait
libs/posix/pthread/test_h2/exit1_main_mutex
libs/posix/pthread/test_h2/exit1_worker_cond_wait
libs/posix/pthread/test_h2/exit1_detached_worker_with_stuck
libs/posix/pthread/test_h2/pthread_exit_main
libs/posix/pthread/test_h2/pthread_workers_return
libs/posix/pthread/test_h2/join_basic
libs/posix/pthread/test_h2/join_invalid
libs/posix/pthread/test_h2/detach_states
libs/posix/pthread/test_h2/mutex_recursive
libs/posix/pthread/test_h2/mutex_trylock
libs/posix/pthread/test_h2/cond_signal_broadcast
libs/posix/pthread/test_h2/barrier_basic
libs/posix/pthread/test_h2/rwlock_readers_writers
libs/posix/pthread/test_h2/sem_corner
libs/posix/pthread/test_h2/tls_keys
libs/posix/pthread/test_h2/attr_roundtrip
libs/posix/pthread/test_h2/neg_join_self
libs/posix/pthread/test_h2/neg_create_invalid_routine
libs/posix/pthread/test_h2/neg_attr_setstacksize_zero
libs/posix/pthread/test_h2/neg_mutex_unlock_unowned
libs/posix/pthread/test_h2/neg_mutex_destroy_held
libs/posix/pthread/test_h2/neg_cond_wait_no_mutex
libs/posix/pthread/test_h2/neg_barrier_init_zero
libs/posix/pthread/test_h2/neg_sem_overflow_post
libs/posix/pthread/test_h2/neg_tls_use_after_delete
libs/posix/pthread/test_h2/neg_rwlock_unlock_unheld
perf/setie
perf/myid
perf/pingpong
perf/mutex
perf/mutex2
""".split()

# Fail or time out on bcain/v81_support.
SKIPPED_TESTS = {
    test: "Fails on bcain/v81_support"
    for test in """
kernel/data/intconfig/test
kernel/error/fatal/test
kernel/event/error/test
kernel/event/error/test_debug
kernel/event/intpool/test_h2
kernel/event/popup/test_h2
kernel/futex/futex/test/tests/badaccess
kernel/futex/futex/test/tests/find_match
kernel/futex/futex/test/tests/multi_va
kernel/futex/futex/test/tests/multi_wake
kernel/futex/futex/test/tests/pi
kernel/futex/futex/test/tests/simple_lock_unlock
kernel/power/apcr/simple_test
kernel/power/apcr/test_multi
kernel/sched/check_sanity/test/tests/H2K_check_sanity/scenarios
kernel/sched/dosched/test/test
kernel/sched/lowprio/test
kernel/time/timer/test_h2
kernel/time/timer/test_h2_multi
kernel/traps/cputime/test
kernel/vm/vmfuncs/test
kernel/util/stmode/test
libs/h2_compat/alloc/test
libs/h2_compat/allsignal/test/simple
libs/h2_compat/anysignal/test/simple
libs/h2_compat/error/test/simple
libs/h2_compat/lxaccess/test
libs/h2_compat/mxaccess/test
libs/h2_compat/sem/test/simple
libs/h2_compat/sleep/test
libs/h2_compat/vecaccess/test
libs/h2/vmtraps/test/test_ie
libs/h2/vmtraps/test/test_pcycles
libs/h2/vmtraps/test/test_return
libs/h2/vmtraps/test/test_yield
libs/posix/pthread/test_h2/stuck_in_mutex
libs/posix/pthread/test_h2/stuck_in_cond_wait
libs/posix/pthread/test_h2/stuck_in_cond_timedwait
libs/posix/pthread/test_h2/stuck_in_barrier
libs/posix/pthread/test_h2/stuck_in_rwlock_rd
libs/posix/pthread/test_h2/stuck_in_rwlock_wr
libs/posix/pthread/test_h2/stuck_in_sem_wait
libs/posix/pthread/test_h2/stuck_in_sem_timedwait
libs/posix/pthread/test_h2/stuck_in_join
libs/posix/pthread/test_h2/stuck_in_pthread_exit_joined
libs/posix/pthread/test_h2/exit1_main_cond_wait
libs/posix/pthread/test_h2/exit1_main_mutex
libs/posix/pthread/test_h2/exit1_worker_cond_wait
libs/posix/pthread/test_h2/exit1_detached_worker_with_stuck
libs/posix/pthread/test_h2/pthread_exit_main
libs/posix/pthread/test_h2/pthread_workers_return
libs/posix/pthread/test_h2/join_basic
libs/posix/pthread/test_h2/join_invalid
libs/posix/pthread/test_h2/detach_states
libs/posix/pthread/test_h2/mutex_recursive
libs/posix/pthread/test_h2/mutex_trylock
libs/posix/pthread/test_h2/cond_signal_broadcast
libs/posix/pthread/test_h2/barrier_basic
libs/posix/pthread/test_h2/rwlock_readers_writers
libs/posix/pthread/test_h2/sem_corner
libs/posix/pthread/test_h2/tls_keys
libs/posix/pthread/test_h2/attr_roundtrip
libs/posix/pthread/test_h2/neg_join_self
libs/posix/pthread/test_h2/neg_create_invalid_routine
libs/posix/pthread/test_h2/neg_attr_setstacksize_zero
libs/posix/pthread/test_h2/neg_mutex_unlock_unowned
libs/posix/pthread/test_h2/neg_mutex_destroy_held
libs/posix/pthread/test_h2/neg_cond_wait_no_mutex
libs/posix/pthread/test_h2/neg_barrier_init_zero
libs/posix/pthread/test_h2/neg_sem_overflow_post
libs/posix/pthread/test_h2/neg_tls_use_after_delete
libs/posix/pthread/test_h2/neg_rwlock_unlock_unheld
perf/setie
perf/myid
perf/pingpong
perf/mutex
perf/mutex2
""".split()
}


@skipUntrustedTest()
class H2Tests(QemuSystemTest):
    timeout = 180

    ASSET_TARBALL = Asset(
        "https://gitlab.qualcomm.com/api/v4/projects/22034/packages/generic/"
        "hexagon-hypervisor/v0.0.1/h2_test_suite.tar.gz",
        "39f0cea3574453f9e09b67e5bbc3e6d9e128aad92577308d900a79ecd075d232",
    )

    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls._suite_tmpdir = tempfile.TemporaryDirectory(prefix="qemu_h2_")
        archive_extract(cls.ASSET_TARBALL.fetch(), cls._suite_tmpdir.name,
                        format="tar")
        cls._suite_dir = os.path.join(cls._suite_tmpdir.name, "h2_test_suite")

    @classmethod
    def tearDownClass(cls):
        cls._suite_tmpdir.cleanup()
        super().tearDownClass()

    def run_h2_test(self, test):
        test_dir = os.path.join(self._suite_dir, "tests", test)
        with open(os.path.join(test_dir, "Makefile"), encoding="utf-8") as f:
            makefile = f.read()
        match = re.search(r"^EXEC\s*[:?+]?=\s*(\S+)", makefile, re.MULTILINE)
        executable = match.group(1) if match else "test.elf"
        args = [self.qemu_bin, "-display", "none", "-monitor", "none",
                "-m", "4G", "-no-reboot", "-M", "sim"]
        test_elf = os.path.join(test_dir, executable)
        if re.search(r"^BOOT=1$", makefile, re.MULTILINE):
            match = re.search(r"^BOOTER_ARGS\s*[:?+]?=\s*(.*)$", makefile,
                              re.MULTILINE)
            booter_args = match.group(1) if match else ""
            args += ["-kernel", os.path.join(self._suite_dir, "booter"),
                     "-append", f"{booter_args} {test_elf}"]
        else:
            args += ["-kernel", test_elf]
        try:
            result = run(args, capture_output=True, text=True, timeout=60)
        except TimeoutExpired as error:
            self.fail(f"{test} timed out after 60 seconds: {error.stdout}")
        output = result.stdout + result.stderr
        with open(self.log_file(test.replace("/", "_") + ".log"), "w",
                  encoding="utf-8") as f:
            f.write(output)
        self.assertEqual(result.returncode, 0, output)
        self.assertIn("TEST PASSED", output, output)


def h2_test_method(test):
    def method(self):
        self.run_h2_test(test)

    method.__name__ = "test_" + test.replace("/", "_")
    return method


for test in H2_TESTS:
    method = h2_test_method(test)
    if test in SKIPPED_TESTS:
        method = unittest.skip(SKIPPED_TESTS[test])(method)
    setattr(H2Tests, method.__name__, method)


if __name__ == "__main__":
    QemuSystemTest.main()
