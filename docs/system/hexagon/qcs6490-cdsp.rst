.. SPDX-License-Identifier: GPL-2.0-or-later

QCS6490 Compute DSP
===================

The ``qcs6490-cdsp`` machine models the Hexagon v68 compute DSP of the
Qualcomm QCS6490 (SM7325 family), as found on the RubikPi 3.  Its layout
comes from values read off the running hardware, not from documentation:
the config table and REV are what the H2 hypervisor reports on the board,
and the L2VIC and QTimer positions were read from the CDSP's Linux with
``/dev/mem``.

The machine contains:

* a v68 core with six hardware threads, 128-entry JTLB, 1 MiB L2, and two
  128-byte HVX contexts
* the config table at ``0x09980000``
* the L2VIC at ``0x0a390000``
* the QTimer, whose access-control frame is at ``0x0a3a0000`` and whose
  three frames follow at ``0x0a3a1000``
* 2 MiB of VTCM at ``0x09c00000``
* DDR at ``0x80000000`` (1 GiB by default)
* a PL011 UART at ``0x10000000``.  The CDSP has no UART; this one exists
  only so that the guest has a console.

The whole 0x100-byte config table in ``hw/hexagon/machine_cfg_qcs6490_cdsp.h.inc``
was read from the hardware.

Booting Linux
-------------

The CDSP runs the H2 hypervisor with Linux as a guest.  When given
``-kernel``, the machine loads the bundled ``hexagon_loadlinux_qcs6490_cdsp``
firmware (the H2 hypervisor at ``0x88f00000``, where the board's CDSP
firmware puts it), loads the kernel ELF at ``0xa1000000``, generates a device
tree, and starts the firmware with the device tree address in ``r1:r0``.  An
initramfs is passed with ``-initrd``.  The kernel sizes its memory from
``mem=``, and ``lpj=`` avoids calibrating its delay loop against H2's
virtual timer::

    $ qemu-system-hexagon -M qcs6490-cdsp \
        -kernel vmlinux -initrd rootfs.cpio \
        -append 'console=ttyAMA0 mem=240M lpj=89124080' \
        -nographic -serial mon:stdio

Linux's ``qcs6490_cdsp_defconfig`` builds a matching kernel.  ``-bios``
selects another firmware image, and ``-bios none`` enters the kernel ELF
directly at its entry point instead.
