# How Linux boots on the QCS6490 CDSP (RubikPi 3) with the replacement firmware

Status as of 2026-09-25.  Everything here was run on the board ("red"), and the same image boots in QEMU
(`-M qcs6490-cdsp`).

The CDSP's own firmware (`cdsp.mbn`) is replaced by one image that carries the H2 hypervisor, a Linux kernel, a
device tree and an initramfs.  The APPS remoteproc loads it through the normal PIL path, and Linux is running on the
DSP less than a second after the DSP starts.

## 1. Build the image (host)

`attic/red_cdsp/newkernel/mkfw.sh <initramfs.cpio> install` runs three steps.

1. **`scripts/hexagon/mkheximg`** (kernel tree) builds one ELF from:
   - the H2 hypervisor: the `pc-bios` loadlinux build, linked at 0x88f00000, with its boot VM at 0x88fc0000;
   - the kernel's `PT_LOAD` segments (`vmlinux`, PHYS_OFFSET 0xa1000000);
   - the DTB and the initramfs.

   The kernel, DTB and initramfs are **not** stored at the addresses they run at.  PIL accepts only segments inside
   the CDSP's reserved region (`cdsp@88900000`, 0x1e00000 = 30 MB), and the guest's RAM (0xa1000000+) is outside it.
   They are stored above H2, at 0x89000000+, behind a 168-byte position-independent **copy loader**, which is the
   ELF's entry point.  The loader's table says where each blob goes:

   | blob | stored at | copied to |
   |---|---|---|
   | kernel segments | 0x89000000+ | 0xa1000000+ |
   | DTB | after the kernel | 0xa1800000 |
   | initramfs | after the DTB | 0xa8000000 |

   `mkheximg` also adds `linux,initrd-start/-end` to the DTB's `/chosen`.
2. **qtestsign `-v6 cdsp`** adds the hash segment.  It does not sign, which is fine because secure boot is off on this
   board.
3. The result is copied to `/usr/lib/firmware/updates/qcom/qcs6490/cdsp.mbn` on red, and the board is rebooted.  The
   stock image is still there as `original-cdsp.mbn` in the same directory.

The initramfs (`rootfs-min.list`) is 2.7 MB and uncompressed: busybox with all its applet links, a static -O1 dropbear
(the -O2 build is miscompiled by LLVM 22.x for Hexagon), and one copy of musl.  The whole image is about 11 MB of the
23 MB available above H2.  Compression is only worth it for a rootfs that will not fit (`COMPRESS=zstd mkfw.sh`).

## 2. APPS side (stock Ubuntu 6.8.0-1077-qcom kernel)

1. `remoteproc` `a300000` (`qcom_q6v5_pas`) requests `qcom/qcs6490/cdsp.mbn`.  The `updates/` firmware directory takes
   precedence over the stock one.
2. `qcom_mdt_load` copies each segment into the CDSP region and rejects any segment outside it.
3. TZ checks the hashes and releases the Q6 with its entry point set to the ELF entry, 0x89000000.
4. remoteproc reports "is now up" about 0.3 s later.  This kernel does not wait for SMP2P `ready` or `handover` from
   the DSP.

## 3. DSP reset state and the copy loader

- One hardware thread is running, with the MMU off and the core on the 19.2 MHz XO clock.
- The loader copies each blob to its destination and zeroes the BSS.
- It then cleans and invalidates the D-cache and I-cache over what it copied.
- Finally it sets r1:r0 to the DTB address and jumps to H2's entry at 0x88f00000.

## 4. H2 and loadlinux

- **H2 entry:** resets `syscfg`, kills and enables the caches, captures the boot registers (r1:r0), configures L2 and
  the TLB from the config table, and starts the boot VM.
- **Boot VM (loadlinux):**
  - starts all hardware threads (`h2_hwconfig_hwthreads_mask(-1)`) and allocates the STLB;
  - counts the running hardware threads from the info call (`INFO_HTHREADS`): 6 on this core;
  - builds the guest VM with one VCPU per running thread and an identity (offset 0) memory map, cacheable write-back
    with L2;
  - reads the DTB address from the captured r1:r0 and checks for the FDT magic;
  - boots the guest at the kernel start (0xa1000000) with the DTB address as its argument.

## 5. Linux (guest, 6 VCPUs)

1. `head.S` saves r1:r0 to `boot_dtb_phys` and sets up the page tables.
2. `setup_arch`:
   - parses the DTB and takes RAM from `mem=240M`;
   - copies the DTB out of the kernel image's init pages before unflattening it.  The unflattened tree points into the
     blob for property names, and the init pages are freed and reused later in boot, after which property names read
     back as garbage.  It uses `unflatten_and_copy_device_tree()`;
   - reserves the initrd;
   - asks H2 for the running-thread mask through the `vmgetinfo` trap (trap1 #26, info code 19) and makes one CPU
     possible for each thread, up to `CONFIG_NR_CPUS` (6).
3. **`postcore_initcall`:** `clk-qcs6490-cdsp` starts the Q6 PLL at 0x0a340000 (L = 0x31, so 940.8 MHz), waits for
   lock, and switches the core clock onto it.  Before this the core runs on the 19.2 MHz XO, about 50 times slower.
   The sequence comes from `RubikPi-HexagonLinux/.../platform/sm7325/clock_init.c`.
4. **`device_initcall`:**
   - **`glink-shmem`:** the DSP initializes the two 64 KB windows at 0xd7c00000 (receive) and 0xd7c10000 (transmit).
     Doorbell to the APPS: write bit 0x20 at 0x0a380000+0x8000.  Doorbell from the APPS: guest IRQ 91.
   - **GLINK core:** it sends `VERSION`.  The `IP_BRIDGE` channel opens automatically once the APPS side answers.
5. **initramfs:** `/init` mounts the pseudo-filesystems and logs to `/dev/kmsg`.  Its `dsp0` setup waits in the
   background until `dsp0` appears and then configures it as 10.42.0.2.  It then starts dropbear and logs a heartbeat
   every 30 s.

Kernel to userspace takes 0.5 s.

## 6. The link to the APPS

`cdsp-net.service` on red (installed by `apps-glink/sync.sh install`) runs `/opt/cdsp/cdsp-net-up.sh` at boot:

1. wait for the CDSP remoteproc (`a300000.remoteproc`, whose number changes between boots) to be `running`;
2. `insmod rpmsg_net.ko`;
3. `insmod qcom_glink_shmem.ko intentless=1`, retried until the DSP has initialized the windows.  The probe fails
   while they are not initialized, before it touches any doorbell;
4. `ip addr replace 10.42.0.1 peer 10.42.0.2 dev dsp0`, then `ip link set dsp0 up`.

By hand, that is:

```
insmod rpmsg_net.ko
insmod qcom_glink_shmem.ko intentless=1
ip addr add 10.42.0.1 peer 10.42.0.2 dev dsp0
ip link set dsp0 up
```

When the APPS module attaches, `VERSION` and `VERSION_ACK` cross.  The DSP's auto-open work then sends `OPEN` for
`IP_BRIDGE`, and both sides register `dsp0`.  The APPS modules are an out-of-tree build of the same two sources as
the DSP's (`rpmsg_net.c`, `qcom_glink_shmem.c`) against red's 6.8 kernel, using its in-tree `qcom_glink_native`.
`intentless=1` is needed because the host's old snull DT node cannot say `qcom,intentless`, and both sides must
agree: with only the DSP intentless, every packet from the APPS is dropped.

`ssh cdsp` from red (alias in `~/.ssh/config`) then reaches the DSP as root, key
`~/.ssh/id_ed25519_cdsp`.  The DSP's dropbear host key changes each boot.  Login takes about 5 s.

Measured with the clock raised and all six CPUs: ping RTT 0.2 ms, 8 MB over ssh in about 1 s.

## 7. Limits

- **No console:** the DSP has none.  Read its kernel log from the APPS through `/dev/mem` at `__log_buf` (about
  0xa1791eac in the current image, but it moves with each kernel build).  `/dev/mem` must be opened `O_SYNC`: the
  kernel then maps it noncached.  A cached mapping of device registers took the whole SoC down before that was fixed.
- **No loop closure:** the DSP sends no SMP2P `ready` or `handover` to the APPS.  The Ubuntu 6.8 driver does not need
  it, and the DSP stayed up for 8+ minutes without it.  A mainline PAS driver waits 5 s for `ready`.  Closing that
  would need SMP2P over SMEM and an IPCC signal from the DSP.
- **Hazards:**
  - `remoteproc stop` on the CDSP resets the SoC: reboot the board instead.
  - The APPS doorbell write (0x17c0000c, bit 0x20) is the same one the old idle-H2 image used as its "start Linux"
    poke.  With this firmware H2 does not idle, and the module does not ring the doorbell until the DSP's windows are
    initialized, so the service is safe.  Do not load the module against the old idle-H2 firmware.
- **Manual reload:** the APPS modules must be loaded once per DSP boot.  Unloading and reloading `qcom_glink_shmem`
  while the DSP is running is not supported, because the DSP has already finished the handshake.

## 8. Where things are

| what | where |
|---|---|
| image builder, PIL layout | `~/src/linux/scripts/hexagon/mkheximg` |
| build/install script, rootfs, notes | `attic/red_cdsp/newkernel/` (`mkfw.sh`, `rootfs-min.list`, `README.md`) |
| APPS-side modules, link script, unit | `attic/red_cdsp/apps-glink/` (`sync.sh`, `cdsp-net-up.sh`, `cdsp-net.service`) |
| DSP GLINK transport, rpmsg_net | `drivers/rpmsg/qcom_glink_shmem.c`, `drivers/net/rpmsg_net.c` |
| DSP core clock | `drivers/clk/clk-qcs6490-cdsp.c` |
| hypervisor (VM sizing) | `~/src/hexagon-hypervisor`, `linux/loadlinux.c`, commit 56de65f4 |
| QEMU machine | `hw/hexagon/qcs6490_cdsp.c`, `pc-bios/hexagon_loadlinux_qcs6490_cdsp` |

## 9. HVX and HMX

The guest kernel manages the coprocessors' per-thread state (kernel branch `bcain/qcs6490_cdsp`):

- **HVX** (128-byte vectors, 2 contexts shared by 6 hardware threads) works: `hvx` sysfs class, lazy allocation on the
  first vector instruction, context taken on the way back to user mode and released on the next kernel entry.
  The QEMU tests `v68_hvx`, `hvx_histogram`, `vector_add_int`, plus `hvx_ctx` (8 to 16 processes sharing the two contexts and
  checking that their vector registers survive) and `hvx1`, pass on the board and in QEMU.
- **HMX**: the module is in and the DSP boots with it, but **executing an HMX instruction on the board hangs the whole DSP**
  (no oops, no ping, the APPS stays up), and with the hypervisor asked to power the coprocessors up
  (`hwconfig(EXTPOWER, 1)`) it reset the SoC.  The VTCM itself is fine (`/dev/uio0`, read/write from the CPU).  The 28
  `hmx_*.c` tests of QEMU's `bcain/hmx` branch are built for the DSP by `attic/red_cdsp/hwtests/build.sh` (buffers moved into the
  VTCM by a shim, `-mv81` encodings), but could not be run past the first one.  Open: the HMX power-up sequence for this
  board, and whether the v81 encodings of those tests mean anything on this v68 core.
- The Linux VM needs the hypervisor's hwconfig trap, which loadlinux did not allow it (h2 commit 51a0c10d), and the kernel needs
  the thread events to reach the modules (see the kernel commits "add a notifier for the events in a thread's life").
- The DSP is at `hwtests.cpio`: `attic/red_cdsp/hwtests/pack.sh` packs the tests into the initramfs, `runtests` runs them
  (`ssh cdsp runtests /tests/hvx`).

## 10. perf and the performance counters

`perf` for the DSP is built from the kernel tree (`tools/perf`, static, 2.5 MB stripped):

```
make -C tools/perf ARCH=hexagon CROSS_COMPILE=hexagon-unknown-linux-musl- \
     CC=hexagon-unknown-linux-musl-clang LD=hexagon-unknown-linux-musl-ld.lld HOSTCC=cc \
     O=/tmp/perf_hex LDFLAGS=-static NO_RUST=1 NO_LIBELF=1 NO_LIBPYTHON=1 NO_LIBPERL=1 NO_LIBUNWIND=1 \
     NO_LIBTRACEEVENT=1 NO_SLANG=1 NO_GTK2=1 NO_LIBNUMA=1 NO_LIBBPF=1 NO_LIBCRYPTO=1 NO_LIBCAP=1 NO_ZLIB=1 \
     NO_LZMA=1 NO_ZSTD=1 NO_JVMTI=1 NO_DEMANGLE=1 NO_LIBBABELTRACE=1 NO_SDT=1 NO_LIBPFM4=1 NO_JEVENTS=1 \
     NO_LIBLLVM=1 NO_CAPSTONE=1 NO_LIBDW=1 NO_LIBAUDIT=1 NO_LIBOPENCSD=1 NO_BPF_SKEL=1 -j16
```

(the tree change needed is the commit "perf tools: build for hexagon").  The kernel has `CONFIG_PERF_EVENTS` and a PMU named
`hexagon` (`arch/hexagon/kernel/perf_event.c`): the core's four counters, through the hypervisor (counters read as global
registers g26-g29, PMUEVTCFG written with the hypervisor's PMU call), counting the whole core on behalf of CPU 0, no sampling.
Run on the board (`attic/red_cdsp/perf/perf-demo.sh`, output in `demo-output.txt`), `pmuwork 0 20` (20M iterations of two packets:
a load and an add, then a store) counts:

```
     40531783      hexagon/committed_packets/
     60793792      hexagon/committed_insts/
     20202290      hexagon/committed_loads/
     20131726      hexagon/committed_stores/
```

that is 2, 3, 1 and 1 per iteration; the loops of two loads or two stores give 40.2M loads and 40.1M stores, and a load of a new
cache line each iteration gives 21.2M `dcache_misses`.  Software events work too (`task-clock`, `page-faults`, ...) and
`perf stat -I` gives intervals.  The event numbers were identified by sweeping 1..128 over the loops (`perf/EVENTS.md`).
Other events can be counted by number, `hexagon/event=0x40/`.  Only four fit at once.

Two things learned: writing PMUEVTCFG does *not* clear the counters here (the driver clears them itself), and the shared-memory
windows of the GLINK link survive a reboot, so an APPS that attached to stale ones after the first boot of a new image got no
`dsp0`: the image's loader now zeroes the windows (`mkheximg --zero`).

## 11. Debug information exported by the DSP's Linux

`dspinfo` (in the DSP's rootfs; `attic/red_cdsp/newkernel/dspinfo`, a sample output from the board in `dspinfo-output.txt`) prints
all of it.  debugfs is mounted by `/init`.

| what | sysfs | debugfs |
|---|---|---|
| core clock and PLL | `/sys/devices/platform/soc/a340000.clock-controller/{core_rate_hz,core_source,pll_rate_hz,pll_locked,pll_mode,pll_l_val,pll_cal_l_val,pll_alpha_val}` | `a340000.clock-controller/{regs,state}`: every register of the block, and a decoded summary; `clk/clk_summary` shows `cdsp_core` at the rate the registers give (940.8 MHz) |
| hypervisor and core | `/sys/kernel/hexagon/{arch,vm_version,hthreads,timer_freq_hz}`, `info/<name>` for each code of the hypervisor's info call (build id, revision, TLB/cache sizes, base addresses, coprocessors), `cpus/cpuN` | `hexagon/{info,cpus}` |
| PMU | `/sys/kernel/hexagon/pmu/{evtcfg,evtcfg1,cfg,stid0,stid1}` | `hexagon/pmu` (with the four counters), `hexagon/tlb` (the hypervisor's TLB miss counters: not counted by this build) |
| HVX, HMX | `/sys/class/{hvx,hmx}/` | |
| GLINK link to the APPS | `/sys/bus/platform/drivers/qcom_glink_shmem/d7c00000.glink-edge/{rx_head,rx_tail,tx_head,tx_tail,irqs,kicks}` | `d7c00000.glink-edge/state` |

The ABI is documented in `Documentation/ABI/testing/`.  On the board the PLL shows locked, `PLL_MODE` 0xd8000005, L 49 (= 940.8 MHz
from the 19.2 MHz reference), core source `pll`.  The info call gives the core as v68 with 6 hardware threads, 128-byte HVX vectors
and 2 HVX contexts, 1 MB of L2 with 128-byte lines, a 128 entry TLB and a 2 MB VTCM at 0x09c00000.

A bug found on the way: the GLINK transport drained its FIFO from `probe` after it had enabled the interrupt, and the drain and the
interrupt handler could then both run the GLINK receive code, skipping a message between them: the peer's `OPEN` for `IP_BRIDGE`
was half consumed, the DSP timed out (`failed to auto-open channel IP_BRIDGE: -110`) and `dsp0` never appeared, on some boots.
It now drains before enabling the interrupt.  (Three boots in a row bring the link up.)
