/*
 * Qualcomm GENI UART (QUP UART)
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_CHAR_QCOM_GENI_UART_H
#define HW_CHAR_QCOM_GENI_UART_H

#include "qemu/typedefs.h"

/*
 * Properties: "chardev" is the serial backend.  "memory" links the memory
 * the DMA engines address, which is the system memory if not set.
 */
#define TYPE_QUP_GENI_UART "qup-geni-uart"

/* Size of the register block; keep in sync with the Rust device model */
#define QUP_GENI_UART_MMIO_SIZE 0x6000

#endif /* HW_CHAR_QCOM_GENI_UART_H */
