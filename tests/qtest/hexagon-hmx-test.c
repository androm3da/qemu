/*
 * Test that HMX instructions are gated on SSR:XE2.
 *
 * A bare-metal guest on the virt machine executes mxclracc. With
 * SSR:XE2 clear it must take a precise exception whose cause is
 * NO_COPROC2_ENABLE (0x18, not the HVX cause 0x16), which the guest's
 * exception handler reports on the serial port. With SSR:XE2 set the
 * instruction executes and the guest reports 'K'.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"

#define HMX_NO_COPROC2_ENABLE   0x18

/* Index of the instruction that sets or clears SSR:XE2 in hmx_guest[]. */
#define GUEST_XE2_WORD          (0x50 / 4)
#define INSN_SETBIT_SSR_XE2     0x8cc1da01  /* r1 = setbit(r1,#26) */
#define INSN_CLRBIT_SSR_XE2     0x8cc1da21  /* r1 = clrbit(r1,#26) */

/*
 * Instruction words, loaded at address 0 (-bios). The reset vector and the
 * precise exception vector (EVB + 2 * 4) each jump to their code.
 *
 *  0x00 reset:   jump start
 *  0x08 precise: jump handler
 *  0x40 start:   r0 = ##0x10000000          // UART
 *  0x48          r1 = ssr
 *  0x4c          r1 = clrbit(r1,#17)        // SSR.EX is set out of reset
 *  0x50          r1 = setbit/clrbit(r1,#26) // SSR.XE2
 *  0x54          ssr = r1
 *  0x58          isync
 *  0x5c          mxclracc
 *  0x60          r2 = #'K'
 *  0x64          memb(r0+#0) = r2
 *  0x68          jump .
 *  0x6c handler: r0 = ##0x10000000
 *  0x74          r1 = ssr
 *  0x78          r1 = and(r1,#0xff)         // SSR[7:0] is the cause
 *  0x7c          memb(r0+#0) = r1
 *  0x80          jump .
 */
static const uint32_t hmx_guest[] = {
    [0x00 / 4] = 0x5800c020,
    [0x08 / 4] = 0x5800c032,
    [0x40 / 4] = 0x01004000, 0x7800c000,
    [0x48 / 4] = 0x6e86c001,
    [0x4c / 4] = 0x8cc1d121,
    [GUEST_XE2_WORD] = INSN_CLRBIT_SSR_XE2,
    [0x54 / 4] = 0x6701c006,
    [0x58 / 4] = 0x57c0c002,
    [0x5c / 4] = 0xa6e0c011,
    [0x60 / 4] = 0x7800c962,
    [0x64 / 4] = 0xa100c200,
    [0x68 / 4] = 0x5800c000,
    [0x6c / 4] = 0x01004000, 0x7800c000,
    [0x74 / 4] = 0x6e86c001,
    [0x78 / 4] = 0x7601dfe1,
    [0x7c / 4] = 0xa100c100,
    [0x80 / 4] = 0x5800c000,
};

typedef struct {
    const char *cpu;
    bool xe2;
    char expect;
} HmxGateTest;

static void test_hmx_gate(gconstpointer data)
{
    const HmxGateTest *t = data;
    g_autofree char *bios = NULL;
    g_autofree char *serial = NULL;
    g_autofree uint32_t *image = g_memdup2(hmx_guest, sizeof(hmx_guest));
    QTestState *qts;
    int fd;
    char ch = 0;
    gint64 deadline;

    image[GUEST_XE2_WORD] = t->xe2 ? INSN_SETBIT_SSR_XE2 :
                                     INSN_CLRBIT_SSR_XE2;
    for (size_t i = 0; i < ARRAY_SIZE(hmx_guest); i++) {
        image[i] = GUINT32_TO_LE(image[i]);
    }

    fd = g_file_open_tmp("qtest-hexagon-hmx-cXXXXXX", &bios, NULL);
    g_assert(fd != -1);
    g_assert(write(fd, image, sizeof(hmx_guest)) == sizeof(hmx_guest));
    close(fd);

    fd = g_file_open_tmp("qtest-hexagon-hmx-sXXXXXX", &serial, NULL);
    g_assert(fd != -1);
    close(fd);

    qts = qtest_initf("-M virt -cpu %s -bios %s -no-shutdown "
                      "-chardev file,id=serial0,path=%s "
                      "-serial chardev:serial0 -accel tcg",
                      t->cpu, bios, serial);

    fd = open(serial, O_RDONLY);
    g_assert(fd != -1);
    deadline = g_get_monotonic_time() + 60 * G_USEC_PER_SEC;
    while (read(fd, &ch, 1) != 1) {
        g_assert(g_get_monotonic_time() < deadline);
        g_usleep(10000);
    }
    close(fd);
    g_assert_cmphex(ch, ==, t->expect);

    qtest_quit(qts);
    unlink(bios);
    unlink(serial);
}

int main(int argc, char **argv)
{
    static const HmxGateTest tests[] = {
        { "v75", false, HMX_NO_COPROC2_ENABLE },
        { "v75", true, 'K' },
        { "v79", false, HMX_NO_COPROC2_ENABLE },
        { "v79", true, 'K' },
        { "v81", false, HMX_NO_COPROC2_ENABLE },
        { "v81", true, 'K' },
    };

    g_test_init(&argc, &argv, NULL);

    if (!qtest_has_machine("virt")) {
        g_test_skip("virt machine not available");
        return g_test_run();
    }

    for (size_t i = 0; i < ARRAY_SIZE(tests); i++) {
        g_autofree char *path = g_strdup_printf("hexagon-hmx/gate/%s/xe2-%d",
                                                tests[i].cpu, tests[i].xe2);

        qtest_add_data_func(path, &tests[i], test_hmx_gate);
    }

    return g_test_run();
}
