#!/bin/sh
# Show perf on the CDSP: software events, the hexagon PMU, and its counters over known loops.
set -x
perf --version
ls /sys/bus/event_source/devices
perf list 2>&1 | grep -a -E "hexagon|cpu-clock|task-clock|context-switches"
# Software events, for a task
perf stat -e task-clock,context-switches,page-faults -- pmuwork 0 20
# The core's counters: whole core, the four counters at once.  Mode 0 is 20M iterations of
# two packets: a load and an add, then a store.  Expect 40M packets, 60M instructions, 20M each of loads and stores.
perf stat -a -e hexagon/committed_packets/,hexagon/committed_insts/,hexagon/committed_loads/,hexagon/committed_stores/ -- pmuwork 0 20
# Mode 1 is two loads per iteration, mode 2 two stores.
perf stat -a -e hexagon/committed_packets/,hexagon/committed_insts/,hexagon/committed_loads/,hexagon/committed_stores/ -- pmuwork 1 20
perf stat -a -e hexagon/committed_packets/,hexagon/committed_insts/,hexagon/committed_loads/,hexagon/committed_stores/ -- pmuwork 2 20
# A load of a new cache line per iteration: the data cache misses, and the cycles.
perf stat -a -e hexagon/dcache_misses/,hexagon/running_cycles/,hexagon/committed_loads/ -- pmuwork 4 20
# The same, in intervals, and by raw event number.
perf stat -a -I 100 -e hexagon/event=0x13/,hexagon/event=0x34/ -- pmuwork 4 20 2>&1 | head -12
