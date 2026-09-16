/*
 * Verify that monitor control-register transfers fault in User mode.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "mmu.h"

#define HEX_CAUSE_PRIV_USER_NO_SINS 0x1b

static uint32_t priv_faults;
static uint32_t htid;

static inline void increment_elr(uint32_t bytes)
{
    asm volatile("r7 = elr\n\t"
                 "r7 = add(r7, %0)\n\t"
                 "elr = r7\n\t"
                 : : "r"(bytes) : "r7");
}

void sys_xfr_priv_error(uint32_t ssr)
{
    if (GET_FIELD(ssr, SSR_CAUSE) != HEX_CAUSE_PRIV_USER_NO_SINS) {
        do_coredump();
    }

    priv_faults++;
    increment_elr(4);
    enter_kernel_mode();
}

MY_EVENT_HANDLE(my_event_handle_error, sys_xfr_priv_error)

DEFAULT_EVENT_HANDLE(my_event_handle_nmi, HANDLE_NMI_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_tlbmissrw, HANDLE_TLBMISSRW_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_tlbmissx, HANDLE_TLBMISSX_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_reset, HANDLE_RESET_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_rsvd, HANDLE_RSVD_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_trap0, HANDLE_TRAP0_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_trap1, HANDLE_TRAP1_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_int, HANDLE_INT_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_fperror, HANDLE_FPERROR_OFFSET)

int main(void)
{
    install_my_event_vectors();
    enter_user_mode();

    asm volatile("%0 = htid\n\t" : "=r"(htid));
    check32(priv_faults, 1);

    enter_user_mode();
    asm volatile("r0 = modectl\n\t" : : : "r0");
    check32(priv_faults, 2);

    printf("%s\n", err ? "FAIL" : "PASS");
    return err;
}
