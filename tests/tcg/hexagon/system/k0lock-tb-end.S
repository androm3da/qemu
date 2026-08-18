/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * Test that a packet ending the TB without a change-of-flow still
 * advances the PC.  k0lock is A_NOTE_NOPACKET (always alone in its
 * packet) and has no COF, so it exercises pkt_ends_tb() without
 * pkt_has_cof.  If the PC is left pointing at the k0lock packet, an
 * uncontested lock succeeds and the next execution of the same
 * instruction sees SYSCFG.K0LOCK already set, hits the "double
 * k0lock" path, and halts the vCPU forever with no other thread to
 * wake it -- this test then hangs instead of reaching stop().
 */

	.section .start, "ax", @progbits
	.p2align 12
	.globl _start
_start:
	r0 = #0
	ssr = r0
	isync

	k0lock

	r2 = #0			/* PASS: reached here means PC advanced */
	stop(r0)
