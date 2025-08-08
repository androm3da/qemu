#
# Copyright(c) 2024-2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
#
# SPDX-License-Identifier: GPL-2.0-or-later

from glob import glob
import os

def read_skip_file(dirname):
    with open(f'{dirname}/SKIP') as f:
        lines = [line.strip() for line in f.readlines()]
        skip_names = filter(lambda line: line[0] != "#", lines)
    return set(skip_names)

def list_test_cases(dirname):
    return glob(f'{dirname}/*.pbn') + glob(f'{dirname}/*.elf')


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

