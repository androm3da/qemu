#
# Copyright(c) 2024-2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
#
# SPDX-License-Identifier: GPL-2.0-or-later

from glob import glob
import os
import textwrap
import sys

def read_skip_file(dirname):
    with open(f'{dirname}/SKIP') as f:
        lines = [line.strip() for line in f.readlines()]
        skip_names = filter(lambda line: line[0] != "#", lines)
    return set(skip_names)

def list_test_cases(dirname):
    # Look for executable files without extensions (systests_standalone format)
    test_cases = []
    for filename in os.listdir(dirname):
        full_path = os.path.join(dirname, filename)
        # Check if it's an executable file
        if os.path.isfile(full_path) and os.access(full_path, os.X_OK):
            # Skip common non-test files
            if filename not in ['SKIP']:
                test_cases.append(full_path)

    # Fallback to legacy formats if no executables found
    if not test_cases:
        test_cases = glob(f'{dirname}/*.pbn') + glob(f'{dirname}/*.elf')

    return test_cases


class HexagonCheckError(Exception):
    pass

def scale_timeout(timeout_sec):
    try:
        import psutil
        load = psutil.cpu_percent(2)
    except ModuleNotFoundError:
        load = 0
    timeout_scale = 1 + (load / 100) if load > 0 else 1
    timeout_sec *= timeout_scale
    return timeout_scale, timeout_sec

def run_tests(test, dirname, timeout, check):
    skip = read_skip_file(dirname)
    success, fail = 0, 0
    timeout_scale, timeout = scale_timeout(timeout)
    print(f'# Per-test timeout: {timeout:.2f}s (scale: {timeout_scale:.2f})')
    # Some qurt tests will output unicode characters
    test.vm.set_encoding("ISO-8859-1")
    for test_bin in list_test_cases(dirname):
        test_name = os.path.basename(test_bin)
        if test_name in skip:
            print(f'#   SKIP {test_name}')
            continue
        print(f'#   Testing {test_name}')
        test.set_vm_arg('-kernel', test_bin)
        test.vm.launch()
        test.vm.wait(timeout=timeout)
        try:
            check(test_name)
            success += 1
        except HexagonCheckError as e:
            fail += 1
            err_msg = f'FAILED (exit code {test.vm.exitcode()})\n' + \
                      '----------\n' + str(test.vm) + '\n----------\n' + \
                      str(e) + '\n----------'
            print(textwrap.indent(err_msg, "#     ", lambda _: True))
    if fail > 0:
        print(f'# FAIL:  {fail}')
    else:
        print(f'# All pass ({success})')
    return fail == 0
