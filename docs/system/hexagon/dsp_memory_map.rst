Hexagon DSP subsystem physical memory map
==========================================

Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.

This note has two parts:

1. How the Hexagon compute-DSP ("cdsp") subsystem, and its siblings, is
   laid out in a real Qualcomm SoC's physical address map, and how that
   correlates with the per-machine memory maps QEMU already encodes in
   ``hw/hexagon/machine_cfg_*.h.inc``.

2. A proposed physical memory map for the software components involved
   in booting Linux on the QEMU ``virt`` machine (``hw/hexagon/virt.c``),
   derived from the trends observed across several SoCs' address maps
   and from a known-working boot flow.

Sources used:

- ``hw/hexagon/machine_cfg_*.h.inc`` and ``include/hw/hexagon/hexagon.h``.
- ``hw/hexagon/virt.c`` and ``hw/hexagon/hexagon_dsp.c``.
- ``target/hexagon/reg_fields_def.h.inc`` and ``hw/hexagon/hexagon_tlb.c``.
- ``tests/functional/hexagon/test_linux.py``, and the ``loadlinux`` ELF
  binary it uses, which are the only known-working, in-tree evidence of
  a full Linux boot on the ``virt`` machine.


Part 1: cdsp subsystem in the SoC physical address map
--------------------------------------------------------

One flat address space, two buses
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Qualcomm SoCs use a single, flat physical address map.  A cdsp
subsystem occupies one aperture in that map, typically 16-96 MiB,
sitting below the DDR window.  Two different buses can reach that same
aperture:

- The subsystem's own local bus/crossbar (``cdsp_nsp_noc`` below)
  connects the Hexagon core directly to its L2TCM, VTCM, L2 config
  space, ETM and CSR blocks, all inside the aperture.

- The chip-wide AXI/NoC fabric lets every other bus master on the SoC
  (the applications CPU, other DSPs, QDSS/debug, etc.) reach the exact
  same addresses, e.g. to load firmware into TCM/VTCM before releasing
  the Hexagon core from reset, or to debug it.

Because both buses resolve to the same physical address, there is no
address translation between "local" and "global" views: what differs is
only which bus a given master uses to get there.  The Hexagon core does
need a way to *discover* those addresses though, since they move between
SoCs and between multiple DSP instances on the same SoC.  That is the
job of the "config table" (see `QEMU's config table constants are the
real SoC addresses`_).

cdsp aperture layout (from the SoC-wide address database)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Module names below use this document's "cdsp" naming; see the SoC's own
register database for the manufacturer's internal names.

sc8480xp ("glymur"), single cdsp instance,
0x30000000-0x32ffffff (48 MiB)::

    0x30000000  L2TCM                         1 MiB
    0x30180000  L2CFG                         512 KiB   (config table here)
    0x31000000  VTCM                          8 MiB
    0x32000000  cdsp_cc_pll                   32 KiB
    0x32008000  cdsp_cc_reg                    80 KiB
    0x32080000  cdsp_tcsr                     124 KiB
    0x320a4000  cdsp_misc1                     64 KiB
    0x320c0000  cdsp_misc2                    ~132 KiB
    0x32260000  cdsp_misc3                     64 KiB
    0x32280000  cdsp_misc4                     64 KiB
    0x322c0000  cdsp_misc5                    256 KiB
    0x32300000  qdsp6v81ss                      1 MiB
      0x32380000  qdsp6ss_csr                    64 KiB
      0x32390000  qdsp6ss_l2vic                   4 KiB
      0x323a0000  qdsp6ssv81_qtmr_rscc           256 KiB
        0x323a1000  qtmr_f0 (QTimer frame 0)       4 KiB

sa8775p ("lemans") has two cdsp instances, each in its own 48 MiB
aperture::

    cdsp_nsp_0      0x24000000  (48 MiB, has VTCM)
    cdsp_nsp_1      0x28000000  (48 MiB, has VTCM)

cdsp_nsp_0 breaks down the same way as glymur's cdsp::

    0x24000000  L2TCM   1 MiB
    0x24180000  L2CFG   512 KiB
    0x25000000  VTCM    8 MiB
    0x26300000  qdsp6v69ss  1 MiB (csr at +0, l2vic +0x90000, qtmr +0xa1000)

QEMU currently models these compute (NSP/CDSP) instances via
``machine_cfg_sa8775_cdsp0.h.inc``, ``machine_cfg_sc8480xp_nsp0.h.inc``,
etc.

QEMU's config table constants are the real SoC addresses
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``include/hw/hexagon/hexagon.h`` defines ``hexagon_machine_config`` /
``hexagon_config_table``, and each ``hw/hexagon/machine_cfg_*.h.inc``
instance is populated straight from the real SoC's global/AXI address
map above, just bit-packed: most ``hexagon_config_table`` fields hold
bits [35:16] of a real physical address (``HEXAGON_CFG_ADDR_BASE(addr)``
extracts them; the field is used as ``field << 16`` everywhere it is
consumed).

Cross-checking ``hw/hexagon/machine_cfg_sc8480xp_nsp0.h.inc`` against
the glymur cdsp aperture above, every constant matches exactly::

    cfgbase       = 0x30000000 + 0x180000 = 0x30180000  == L2CFG base
    l2tcm_base    = 0x00003000 << 16       = 0x30000000  == L2TCM base
    vtcm_base     = 0x00003100 << 16       = 0x31000000  == VTCM base
    subsystem_base= 0x00003238 << 16       = 0x32380000  == qdsp6ss_csr
    csr_base      = 0x32380000                           == qdsp6ss_csr
    l2vic_base    = 0x32300000 + 0x90000  = 0x32390000  == qdsp6ss_l2vic
    qtmr_region   = 0x32300000 + 0xa1000  = 0x323a1000  == qtmr_f0

The same check on ``machine_cfg_sa8775_cdsp0.h.inc`` against cdsp_nsp_0
on lemans matches equally exactly (cfgbase 0x24180000 == L2CFG,
l2tcm_base 0x24000000 == L2TCM, vtcm_base 0x25000000 == VTCM, csr_base
0x26300000 == qdsp6v69ss base).

In other words: QEMU's per-SoC machine_cfg files are not synthetic --
they are the real chip's global/AXI addresses for that cdsp instance,
re-encoded into the compact config-table format that real firmware reads
out of ROM at 0x30180000 (or wherever ``cfgbase`` points) to self-discover
its own TCM/VTCM/CSR/L2VIC addresses.  QEMU reproduces this exactly:
``hw/hexagon/virt.c``'s ``virt_init()`` builds the same table and drops
it as a ROM blob at ``m_cfg->cfgbase`` (see the
``rom_add_blob_fixed_as("config_table.rom", ...)`` call).

DDR and other global-only regions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The cdsp aperture and its siblings (lpass, wpss, ...) sit in the low
part of the SoC's address map; DDR is a separate, much larger region
that both the DSP and the applications CPU reach only via the chip-wide
AXI/NoC (there is no "local" view of DDR from inside a DSP subsystem).
Across both SoCs surveyed, DDR consistently starts at 0x80000000 (2 GiB):

.. code-block:: text

    sc8480xp ("glymur"): DRAM_LOW 0x80000000, size 0x80000000 (2 GiB)
                          DRAM_MID 0x880000000 (34 GiB), up to 64 GiB
                          DRAM_UPPER 0x8800000000 (~544 GiB)
    sa8775p ("lemans"):  DDR_space 0x80000000, size 0x380000000 (14 GiB)
                         DDR_space_1 0x800000000 (32 GiB)

The SoC's carve-out for a given DSP subsystem's firmware/heap in DDR (as
opposed to the DSP's own on-chip TCM/VTCM) is not a static entry in these
address databases -- it is negotiated at boot time between the boot
firmware (XBL) and each subsystem via SMEM, so it does not have a fixed
address to correlate against.

What actually fits in a Hexagon TLB entry
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The above addresses matter for QEMU because the Hexagon MMU's physical
address reach is limited.  From ``target/hexagon/reg_fields_def.h.inc``,
a TLB entry's physical page number is built from PTE_PPD (24 bits) plus
either:

- PTE_PA35 (1 bit) when PTE_HSV39 is clear: 25 bits of page number, and
  with the common TARGET_PAGE_BITS=12 (4 KiB pages) that is a 37-bit
  physical address space (up to 128 GiB), matching this project's
  understanding that Hexagon provides "~37 bits" of physical addressing.

- PTE_PA43 + PTE_PA4544 (3 bits total) when PTE_HSV39 is set: 27 bits
  of page number, i.e. a 39-bit physical address space (up to 512 GiB).

See ``hw/hexagon/hexagon_tlb.c:GET_PPD()`` for the exact bit assembly.

Checked against the DDR regions above: DRAM_LOW and DRAM_MID (up to
64 GiB) both fit inside the 37-bit legacy reach; DRAM_UPPER (~544 GiB)
does not fit even in 39-bit (Sv39) mode.  ``hw/hexagon/hexagon_dsp.c``'s
"cpz" test region, deliberately placed at 0x910000000 (~36 GiB, "test
region for cpz addresses above 32-bits", see ``hexagon_dsp.c``) is a good
example of a real-looking but purely test address: it exercises the
above-32-bit code paths while staying safely inside the 37-bit limit.


Part 2: Proposed memory map for a Linux boot on ``virt``
------------------------------------------------------------

What the ``virt`` machine already fixes in place
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``hw/hexagon/virt.c``'s ``base_memmap[]`` and ``virt_init()`` already
commit to a handful of fixed addresses, none of which a boot flow can
move:

.. code-block:: text

    0x00000000              DDR RAM, size = -m (default 4 GiB)
    0x10000000  (256 B)      VIRT_UART0  (pl011)
    0x11000000  (16 MiB)     VIRT_MMIO   (8 virtio-mmio slots, 16 MiB each)
    0x26300000  (4 KiB)      VIRT_PLL    (cdsp-fabia-pll)
    0x99800000  (4 MiB)      VIRT_FDT    (generated or -dtb device tree)
    0x99c00000  (512 B)      VIRT_BOOT   (built-in bootloader shim, see
                             `The two-stage boot: "h2" shim -> loadlinux
                             -> vmlinux`_)
    0xab000000  (4 KiB)      VIRT_GPT    (generic purpose timer)
    m_cfg->cfgbase           config table ROM (0xde000000 for v68n_1024,
                             the config used by ``virt``)

(l2vic and QTimer register windows are also fixed by the active
``hexagon_machine_config``, e.g. 0xfc910000/0xfc900000/0xfc921000 for
v68n_1024; they are unrelated to Linux's own image placement.)

RAM is a single region spanning [0, ram_size); the fixed windows above
are carved out of it as higher-priority overlapping subregions, so any
software placement below must avoid them.

The two-stage boot: "h2" shim -> loadlinux -> vmlinux
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The only in-tree, known-working example of a Linux boot is
``tests/functional/hexagon/test_linux.py``.  It shows the real flow:

1. CPU reset: ``boot-evb`` is programmed (by ``virt_init()``) to point at
   VIRT_BOOT (0x99c00000), where a tiny 9-instruction shim is stored
   (the ``bootloader[]`` array in ``virt.c``).  This shim's whole job is
   to load r1:0 with the FDT's physical address and jump to the entry
   point of whatever ``-kernel`` file was given ("h2 can forward this
   [FDT] address to its guests", commit 750e3c2fb9).

2. ``-kernel loadlinux``: an ELF, loaded via ``load_elf_ram_sym()`` at
   its own linked physical addresses.  This is the "h2 kernel" -- a
   small booter that sets up its own EVB/TLB/caches (crt0_standalone)
   and per-thread state, then jumps to Linux.  Inspecting the in-tree
   ``loadlinux`` binary (readelf/nm) shows its real physical layout:

   .. code-block:: text

       0x9b800000  .entry   (tiny, __entry / __ENTRY_SECTION_PA)
       0x9b802000  .text
       0x9b80b000  .rodata
       0x9b80c000  .data
       0x9bc00000  .bootvm  (__bootvm_entry_point, 0x13348 bytes,
                             ends 0x9bc13348)
         0x9bc11000  heapBase/heapLimit
         0x9bc11008  stackBase/stackLimit
         0x9bc163c8  linux_vcpu_stacks   (per-vCPU boot stacks, for Linux)

   (DEFAULT_STACK_SIZE = 1 MiB, DEFAULT_HEAP_SIZE = 64 MiB, per the
   symbol table -- budgets used when h2 sizes those runtime areas.)

3. ``-device loader,addr=0xa0000000,file=vmlinux.bin``: the actual Linux
   kernel image (raw binary, not compressed) is placed directly by the
   generic loader device at a fixed physical address, 0xa0000000.  h2
   jumps here once its own setup is done.  This is "linux kernel (h2
   guest)".  Once running, Linux uses the l2vic/QTimer objects via the
   paravirtualized "qcom,h2-pic,hvm-pic" / "qcom,h2-timer,hvm-timer"
   FDT-compatible strings (see ``fdt_add_hvm_pic_node()``/
   ``fdt_add_gpt_node()`` in ``virt.c``) -- this is the "clocksource:
   Switched to clocksource HVM timer" line the functional test waits
   for.

That test does not use an initramfs; it boots off a virtio-blk qcow2
disk instead.  `Proposed layout`_ below proposes where an initramfs
would go if one is used instead.

Proposed layout
~~~~~~~~~~~~~~~~

Building on the working addresses above, and leaving generous headroom
so that different kernel/h2 builds do not collide with the fixed windows
above, for a machine started with ``-m 4G``:

.. code-block:: text

    0x00000000  DDR RAM start.  Free for h2/loadlinux's own image (it is
                position-independent enough to link wherever it likes,
                as shown by the real loadlinux example landing around
                2.4 GiB in -- there is no requirement it start at 0).
    0x10000000  VIRT_UART0 / VIRT_MMIO windows (fixed, avoid).
    0x26300000  VIRT_PLL (fixed, avoid).
    0x40000000  (recommended) h2/loadlinux image + heap + per-vCPU boot
                stacks.  Budget ~80 MiB: a few hundred KiB of code/data,
                1 MiB/thread stacks (THREADS_MAX = 16 in target/hexagon/
                cpu.h, so <=16 MiB), plus DEFAULT_HEAP_SIZE-class 64 MiB.
    0x99800000  VIRT_FDT (fixed, 4 MiB window, avoid).
    0x99c00000  VIRT_BOOT shim (fixed, 512 B, avoid).
    0xa0000000  linux kernel (h2 guest), i.e. vmlinux.bin -- matches the
                known-working functional test exactly.  Budget: leave at
                least 64-128 MiB before the next fixed item so an
                uncompressed kernel image plus its .bss has room.
    0xab000000  VIRT_GPT (fixed, 4 KiB, avoid -- falls inside the 64-128
                MiB kernel headroom above; keep the kernel image itself
                under ~0xaaf00000 or treat VIRT_GPT as a deliberate hole).
    0xc0000000  initramfs (if used instead of/alongside a virtio-blk
                disk), 4 MiB aligned per ``hexagon_load_initrd()``'s own
                ``QEMU_ALIGN_UP()``.  At 0xc0000000 there is 1 GiB of
                room before the end of a 4 GiB RAM configuration,
                comfortably fitting a 128-256 MiB initramfs with slack
                for a larger one.
    0x100000000 end of RAM (with the default ``-m 4G``).

Caveat -- initrd auto-placement can collide with a fixed vmlinux load
address: ``hexagon_load_initrd()`` (``hw/hexagon/virt.c``) computes its
own start address from the *kernel ELF's* image_high_addr (the
loadlinux image in this flow, whose image_high_addr is ~0x9bc13348),
not from wherever vmlinux.bin was separately placed by ``-device
loader``.  For a 4 GiB RAM machine that formula gives
``align_up(0x9bc13348 + 64 MiB, 4 MiB)`` = 0xa0000000 -- the exact
address used for vmlinux.bin above.  If ``-initrd`` is combined with
this two-stage h2+loader-device boot flow unmodified, the automatic
placement will land the initramfs on top of vmlinux and silently
corrupt it (whichever loads last wins).  Until ``virt.c`` is taught
about the separately-loaded guest kernel's real address/size, either
avoid ``-initrd`` with this flow (use virtio-blk, as the existing
functional test does) or pass an explicit, non-overlapping initrd
address through a mechanism that bypasses
``hexagon_load_initrd()``'s automatic placement.

Kernel stacks
~~~~~~~~~~~~~~

Two distinct things are called "kernel stack" in this flow, and both are
allocated by h2/loadlinux, not by QEMU:

- The per-vCPU *boot* stacks h2 hands to Linux before jumping to
  vmlinux's entry point (``linux_vcpu_stacks`` in the loadlinux example,
  living in h2's own .bss just past its image, budgeted at
  DEFAULT_STACK_SIZE = 1 MiB per hardware thread).  Linux's head.S-style
  entry code runs on one of these until it switches to its own
  statically- or dynamically-allocated per-CPU stacks.

- Linux's own per-CPU kernel stacks, which come out of the kernel's own
  memory allocator once it is running, and are not something a boot
  loader or QEMU need to place explicitly.

QEMU has no visibility into either: both are managed entirely by the
guest software stack (h2 then Linux) once execution leaves the tiny
built-in bootloader shim.
