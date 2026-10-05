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
* the Linux kernel address, ``0xa1000000``, which is where the machine's
  H2 loadlinux expects it
* the console and virtio-mmio transports common to the
  :doc:`Qualcomm DSP machines <qcom-dsp>`

The whole 0x100-byte config table in ``hw/hexagon/machine_cfg_qcs6490_cdsp.h.inc``
was read from the hardware.

Linux boots on it as described for the other
:doc:`Qualcomm DSP machines <qcom-dsp>`.  Linux's ``qcs6490_cdsp_defconfig``
builds a matching kernel.
