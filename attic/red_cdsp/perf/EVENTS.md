# Hexagon PMU events on the RubikPi 3's v68 core

`perf stat -a -e hexagon/event=<n>/` counts event `n` (the byte of PMUEVTCFG) for the whole core.
`data/sweep_modeN.txt` holds the count of every event 1..128 over each workload of `pmuwork.c`
(20M iterations; mode 0: 2 packets with a load, an add and a store; 1: two loads; 2: two stores;
3: four adds; 4: a load of a new cache line each; 5: a taken branch).  The events that the driver
names, and the counts that identify them (N = 20M):

| event | name | mode 0 | mode 1 | mode 2 | mode 3 | mode 4 |
|---|---|---|---|---|---|---|
| 0x03 | committed_packets | 40.5M | 40.7M | 40.5M | 40.8M | 63M |
| 0x2a | committed_insts | 60.8M | 41.1M | 40.8M | 81.0M | 114M |
| 0x30 | committed_loads | 20.2M | 40.3M | 0.19M | 0.3M | 28M |
| 0x31 | committed_stores | 20.1M | 0.16M | 40.1M | 0.16M | 6.4M |
| 0x39 | endloop_packets | 20.0M | 20.0M | 20.0M | 20.0M | 0.9M |
| 0x34 | running_cycles | 61M | 61M | 61M | 61M | 121M |
| 0x13 | dcache_misses | 12k | 18k | 13k | 20k | 21.3M |

`running_cycles` counts cycles in which a hardware thread is running (18.7M in 2 s of an idle
system, 61M for a 65 ms loop at 940 MHz).  Other events with obvious counts (0x2b/0x2c: ALU
operations, 0x21 and 0x7c: something that a miss per load also triggers) were left unnamed.
The names of the itrace macros in the Hexagon SDK do not map to the numbers by a fixed offset.
