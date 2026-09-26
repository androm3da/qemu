# Booting the modern kernel (~/src/linux, branch bcain/qcs6490_cdsp) on the physical CDSP, with ssh

Status 2026-09-25: boots to userspace on 4 VCPUs, and **ssh works** over the ported shared-memory network
driver (`drivers/net/ethernet/qualcomm/snull.c`; `sn0` = 192.168.1.4 on the DSP, 192.168.1.3 on the APPS).

Status 2026-09-25 (later): the link now runs **IP over GLINK** instead: `rpmsg_net` (`dsp0`, 10.42.0.2 on the DSP,
10.42.0.1 on the APPS) on the "IP_BRIDGE" channel of a GLINK edge carried by `drivers/rpmsg/qcom_glink_shmem.c` over
the same shared windows and doorbells snull used.  The DSP side is all in-tree (`RPMSG_QCOM_GLINK_SHMEM`,
`RPMSG_NET`); the APPS side is an out-of-tree build of the same two sources against the stock 6.8 kernel, using its
in-tree `qcom_glink_native` (see `../apps-glink/`, `sync.sh` copies the sources from ~/src/linux and builds on red).
Ping and ssh work; an 8 MB transfer over ssh checks out.  Step 4 becomes, on red, **after the DSP is up**:
`cd ~/apps-glink && sudo insmod rpmsg_net.ko && sudo insmod qcom_glink_shmem.ko intentless=1` (`intentless=1`
because the host's old snull DT node cannot say `qcom,intentless`; both sides must agree, otherwise the APPS waits
for receive intents the DSP never sends and every packet is dropped), then
`sudo ip addr add 10.42.0.1 peer 10.42.0.2 dev dsp0; sudo ip link set dsp0 up` and
`ssh -i ~/.ssh/id_ed25519_cdsp root@10.42.0.2` (the DSP's dropbear host key changes each boot).
Load the APPS modules once per DSP boot; the DSP initializes the windows and opens the channel itself.
Debugging aid: the DSP log has no console, read `__log_buf` from the APPS as below.

## Procedure (order matters!)
1. Build `vmlinux` + `dtbs` (`qcs6490_cdsp_defconfig`), `llvm-objcopy -O binary vmlinux vmlinux.bin`.
2. Initramfs: buildroot rootfs, pruned (drop `usr/share/zsh`: 1300 tiny files exhaust the tmpfs limit at 64K pages
   and `/var` etc. silently fail to unpack), `init` from this dir as /init, the **-O1 static dropbear** (see below),
   `/root/.ssh/authorized_keys`, root password `root` in /etc/shadow.  Pack with the kernel's `usr/gen_init_cpio`
   from `rootfs.list`.
3. `deploy_dsp.sh initramfs.cpio [vmlinux.bin]` (host): fdtput the initrd range into the DTB, scp to red:~/newk,
   **reboot the board**, run `~/newk/boot.sh` (loads the images through an *uncached* mapping, see memwrite.py), poke.
4. On red, **after the DSP is up**: `cd ~/hexagon && sudo insmod snull.ko && sudo ifconfig sn0 192.168.1.3 ...`
   then `ssh hexagon` (password root) or `ssh -i ~/.ssh/id_ed25519_cdsp root@192.168.1.4`.
5. No console: read the kernel log from the APPS: `sudo dd if=/dev/mem bs=4096 skip=$((PA/4096)) count=80 | strings`
   where PA = `__log_buf` - 0xc0000000 + 0xa1000000 (System.map; 0xa1791eac for the current build).

## Hazards learned the hard way
* **Never load the APPS snull.ko (or qcom_glink_shmem.ko) / bring up sn0 while the CDSP is idle in h2**: the APPS doorbell is the *same*
  IPC register/bit as the "start Linux" poke (0x17c0000c, 0x20); the board reset itself repeatedly.
  `remoteproc stop` while the new kernel runs also reset the SoC: reboot the board instead of restarting the CDSP.
* remoteproc numbering changes between boots (a300000 was remoteproc1, then 2): look it up by name (boot.sh does).
* Load images with the uncached mmap writer, not `dd` to /dev/mem: the CDSP is not coherent with APPS caches.
* /tmp on red is wiped by each reboot (askpass and hx helpers).
* Reading the config table past 0x100 wedges h2 (see ../h2patch).

## Findings
* The initramfs unpacked incompletely (no /var): tmpfs size limit x 64K pages (fixed by pruning the rootfs).
* **ssh failed ("incorrect signature") and ECDSA keygen produced "Bad key" with the stock rootfs dropbear.**  Not the
  kernel, not the hardware, not the driver: reproduced with no guest kernel at all (`qemu-hexagon` linux-user +
  the host ssh client).  It is an **LLVM 22.x Hexagon -O2 miscompile**: dropbear (with libtomcrypt/libtommath) built
  with -O0, -O1 or -Os works, -O2 fails; `--disable-packetizer` / `-disable-nvjump` do not help.  All four
  buildroot trees' dropbear 2026.91 fail the same way.  Rebuilt static at -O1: `dropbear/build.sh L1 1 ""`, test with
  `dropbear/sshtest.sh L1 256`.  Worth a compiler bug report (bisect with -opt-bisect-limit).
* Red herring: hashing a *running executable's own file* gives different results run to run, and copies of it
  misbehave.  The hardware stores extra decode information in cached instruction lines, so data reads of executed
  code drift from the file contents (same on the old kernel).  Test hashes on non-executable data.
* The old kernel's `USR=0x56000` forcing is irrelevant to correctness (tested, reverted).
* **The bootloader's DTB sits in the init pages** (0xa1001200, in the gap before .init.text) and the unflattened tree keeps
  pointers into it; after free_initmem() property *names* read back as garbage, so late lookups fail (a work item
  found "no qcom,glink-auto-open" 9 seconds into boot).  Fixed in the kernel with unflatten_and_copy_device_tree().

## Firmware image (2026-09-25): H2 + kernel + DTB + initramfs as the CDSP's own `cdsp.mbn`
`mkfw.sh <initramfs.cpio> install` (uses `scripts/hexagon/mkheximg --pil` from the kernel branch, then qtestsign, then
copies to `/usr/lib/firmware/updates/qcom/qcs6490/cdsp.mbn` on red and reboots the board).  At boot the APPS
remoteproc loads it through the normal PIL path and starts the DSP; Linux is up ~35 s later (most of it gunzipping the
initramfs), with `dsp0` waiting for the APPS module.  No `boot.sh`/poke/deploy_dsp.sh needed any more.
* PIL only accepts segments inside the CDSP's reserved region (`cdsp@88900000`, 0x1e00000 = 30 MB), and the guest's RAM
  (0xa1000000+) is outside it.  So mkheximg --pil keeps H2 where it is (0x88f00000) and stores kernel, DTB and the
  initramfs at 0x89000000+ behind a 168-byte position-independent copy loader, which is the ELF entry point:
  it copies them to 0xa1000000 / 0xa1800000 / 0xa8000000, cleans+invalidates the caches, and enters H2 with r1:r0 = DTB.
  Total ~11 MB of the 23 MB free above H2.  Tested first in QEMU (`-M qcs6490-cdsp -bios none -kernel image.elf`,
  with the QEMU-generated DTB: `-M qcs6490-cdsp,dumpdtb=x.dtb`).
* The initramfs must not sit right after the DTB: at 0xa1900000 the kernel reports "INITRD overlaps in-use memory"
  (its reserved image region extends to about there); 0xa8000000 works.
* H2 is the pc-bios loadlinux build (h2-bcain-18-july-2026), not the board's old source-less one.
* Loop closure: this APPS kernel (6.8.0-1077-qcom) does **not** wait for the SMP2P `ready` (or `handover`) from the
  DSP: "powering up" -> "is now up" takes 35-270 ms with no ready IRQ, and the DSP stayed up (remoteproc `running`)
  for 8+ minutes with no SMP2P at all.  A mainline pas driver waits 5 s for `ready`; closing that would need SMP2P over
  SMEM and an IPCC signal from the DSP, which is what we avoid (IPCC reads from the DSP reset the SoC).
* ssh login over dsp0 takes ~12 s (crypto on the DSP); use the default ssh timeouts (ConnectTimeout=5 abandons the
  banner exchange, and dropbear logs "Exit before auth").
* Restore the stock firmware: `sudo cp /usr/lib/firmware/updates/qcom/qcs6490/original-cdsp.mbn .../cdsp.mbn`.
* **The DSP core runs at 19.2 MHz** (the XO), measured with a UPCYCLE probe (`clk.c`): 4.4 cycles/iteration, 2M iterations in
  0.455 s.  Nothing raises the clock (not TZ, not h2, not the board's old h2 either; same figure in every flow).  So
  anything CPU-bound is ~50x slower than a real DSP (ssh login ~13 s, dropbear crypto), and **compressing the initramfs makes
  boot slower, not faster**: gzip 36 s, zstd 44 s to userspace vs 7 s for a small uncompressed cpio (unpacking is a memcpy).
  Hence `rootfs-min.list` (2.7 MB: busybox with all applet links, static -O1 dropbear, musl once) and no compression by default;
  `COMPRESS=zstd mkfw.sh` remains for a rootfs that does not fit.  Raising the clock (Q6 PLL / QDSP6SS at 0x0a300000) would help
  everything and is the obvious next lever, but nobody has looked at the registers yet.
* NO_STLB h2 makes no difference; the board's original h2 in the same image needs the APPS poke (0x17c0000c) to start
  Linux, so it is not a drop-in; the pc-bios h2 starts it immediately.

### Clock (2026-09-25, later)
The reference `RubikPi-HexagonLinux/.../platform/sm7325/clock_init.c` is the recipe: the core comes out of reset on the 19.2 MHz XO;
start the Turing Q6 PLL at 0x0a340000 (L=0x31 -> 940.8 MHz, CAL_L=0x44, config words in the code), wait for LOCK_DET, then set
CORE_CFG_RCGR src=2 div=1 and pulse CORE_CMD_RCGR.  Now the in-tree driver `drivers/clk/clk-qcs6490-cdsp.c` (DT node
`clock-controller@a340000`, postcore_initcall): kernel to userspace in 0.43 s instead of 7 s, ping rtt 0.2 ms (was 2.3), 8 MB over
ssh in ~1 s (was 32 s), ssh login ~5.6 s.
* Trap: the registers can not be poked through /dev/mem unless the kernel maps it noncached: hexagon had no pgprot_noncached(), so an
  O_SYNC mmap got the cached WB_L2 attribute and the first access **reset the whole SoC** (a cache line fill of a register block).  Fixed
  in the kernel (`hexagon: map /dev/mem noncached when it is opened O_SYNC`).  This is probably also why the earlier "read of the IPCC
  window from the DSP resets the SoC" happened (that used /dev/mem); ioremap() mappings are device-attribute and fine.
* `q6cc.c` is the userspace probe used to validate the sequence (`dump` is read-only; `juice` does the switch).  Its UPCYCLE-based
  MHz print does not follow the switch (reads 19.2 MHz before and after); time a shell loop instead (20000 it/s vs 360 it/s).
* The earlier "compression makes boot slower" conclusion holds only at 19.2 MHz; with the clock up an uncompressed small rootfs is
  still simplest.

### CPUs (2026-09-25, later)
All 6 hardware threads are now Linux CPUs.  loadlinux sizes the Linux VM from the running-thread mask (h2 commit 56de65f4, was a
fixed 4) and the kernel takes its possible CPUs from the same mask via the vmgetinfo trap (info code 19, HTHREADS), up to
CONFIG_NR_CPUS=6.  `/sys/devices/system/cpu/online` = 0-5 on red; 6 parallel busy loops take 2.6 s wall for 15.4 s of CPU.
QEMU needed a fix to see the same: `start` set a thread's MODECTL enable bit only when that thread later ran, so h2 read a partial
mask (0xf, racy); now set synchronously, and the machine's FDT describes all smp.cpus.  pc-bios loadlinux rebuilt from h2 56de65f4
(stripped with `hexagon-strip -s`; build in a git worktree with `make -C linux H2DIR=$PWD KERNELPATH=$PWD/artifacts/v68/opt/build/kernel ...`).

### HVX / HMX (2026-09-25, later)
HVX context management, HMX context management and the VTCM UIO driver are ported from the kernel's bcain/hmx branch (with the
thread-event hooks it lacked: nothing raised the coprocessor fault, and the switch hook deadlocked under the runqueue lock, so it
is gone).  loadlinux now lets the Linux VM make the hwconfig trap (h2 51a0c10d), without which HVX faults for ever.  QEMU's
qcs6490-cdsp /soc needed `compatible = "simple-bus"` for the drivers to probe.  HVX tests pass on the board; **HMX
instructions hang the whole DSP** (see ../../../cdsp_linux_process.md, section 9).  Tests: `../hwtests/`.

### perf (2026-09-26)
tools/perf built for hexagon and a PMU driver for the core's counters (see ../../../cdsp_linux_process.md section 10, ../perf/).
The first boot after installing a new image used to fail to bring up dsp0 (stale GLINK windows in DDR): mkfw.sh now passes
`--zero 0xd7c00000:0x20000` to mkheximg.
