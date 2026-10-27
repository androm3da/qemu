/*
 * Test that the HMX state survives a snapshot.
 *
 * A bare-metal guest on the virt machine accumulates a matrix multiply
 * and waits.  The test saves a snapshot, has the guest clear the
 * accumulators, loads the snapshot back, and lets the guest convert and
 * store the accumulators.  The result is only right if the accumulators
 * were saved and restored.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"

/* Guest memory used by the program below. */
#define GUEST_OUT       0x7000  /* the stored result */
#define GUEST_FLAG      0x8000  /* 1: convert and store, 2: clear */

/* 3 * 5 over 32 channels is 480; the byte convert shifts it right by 4. */
#define EXPECTED_BYTE   30

/*
 * The guest, assembled from the source below and loaded at address 0
 * (-bios).  It writes 'R' to the UART once the multiply is done, then polls
 * GUEST_FLAG: 2 makes it clear the accumulators and write 'C', 1 makes it
 * convert and store them at GUEST_OUT and write 'K'.
 *
 *  start:  ssr.xe2 = 1
 *          memset(0x4000, 3, 2048)     // activations
 *          memset(0x5000, 5, 1024)     // weights
 *          memset(0x6000, 0x5000, 128) // bias
 *          mxclracc; bias=mxmem(0x6000); activation.ub; weight.b
 *          uart = 'R'
 *  wait:   poll GUEST_FLAG ...
 */
static const uint32_t hmx_guest[] = {
    [0x000 / 4] = 0x5800c020, /* reset: jump 0x40 <start> */
    [0x040 / 4] = 0x01004000, /* start: immext(#0x10000000) */
    [0x044 / 4] = 0x7800c000, /* r0 = ##0x10000000 */
    [0x048 / 4] = 0x6e86c001, /* r1 = ssr */
    [0x04c / 4] = 0x8cc1d121, /* r1 = clrbit(r1,#0x11) */
    [0x050 / 4] = 0x8cc1da01, /* r1 = setbit(r1,#0x1a) */
    [0x054 / 4] = 0x6701c006, /* ssr = r1 */
    [0x058 / 4] = 0x57c0c002, /* isync */
    [0x05c / 4] = 0x00004100, /* immext(#0x4000) */
    [0x060 / 4] = 0x7800c002, /* r2 = ##0x4000 */
    [0x064 / 4] = 0x00304c0c, /* immext(#0x3030300) */
    [0x068 / 4] = 0x7800c063, /* r3 = ##0x3030303 */
    [0x06c / 4] = 0x6910c008, /* loop0(0x70,#0x200) */
    [0x070 / 4] = 0x7f008000, /* nop */
    [0x074 / 4] = 0xab82c308, /* memw(r2++#0x4) = r3 */
    [0x078 / 4] = 0x00004140, /* immext(#0x5000) */
    [0x07c / 4] = 0x7800c002, /* r2 = ##0x5000 */
    [0x080 / 4] = 0x00505414, /* immext(#0x5050500) */
    [0x084 / 4] = 0x7800c0a3, /* r3 = ##0x5050505 */
    [0x088 / 4] = 0x6908c008, /* loop0(0x8c,#0x100) */
    [0x08c / 4] = 0x7f008000, /* nop */
    [0x090 / 4] = 0xab82c308, /* memw(r2++#0x4) = r3 */
    [0x094 / 4] = 0x00004180, /* immext(#0x6000) */
    [0x098 / 4] = 0x7800c002, /* r2 = ##0x6000 */
    [0x09c / 4] = 0x00004140, /* immext(#0x5000) */
    [0x0a0 / 4] = 0x7800c003, /* r3 = ##0x5000 */
    [0x0a4 / 4] = 0x6901c008, /* loop0(0xa8,#0x20) */
    [0x0a8 / 4] = 0x7f008000, /* nop */
    [0x0ac / 4] = 0xab82c308, /* memw(r2++#0x4) = r3 */
    [0x0b0 / 4] = 0xa6e0c011, /* mxclracc */
    [0x0b4 / 4] = 0x00004180, /* immext(#0x6000) */
    [0x0b8 / 4] = 0x7800c008, /* r8 = ##0x6000 */
    [0x0bc / 4] = 0x9208c3ff, /* bias=mxmem(r8) */
    [0x0c0 / 4] = 0x00004100, /* immext(#0x4000) */
    [0x0c4 / 4] = 0x7800c002, /* r2 = ##0x4000 */
    [0x0c8 / 4] = 0x7800cf83, /* r3 = #0x7c */
    [0x0cc / 4] = 0x9202c3ec, /* activation.ub=mxmem(r2,r3) */
    [0x0d0 / 4] = 0x00004140, /* immext(#0x5000) */
    [0x0d4 / 4] = 0x7800c004, /* r4 = ##0x5000 */
    [0x0d8 / 4] = 0x000043ff, /* immext(#0xffc0) */
    [0x0dc / 4] = 0x7800c7e5, /* r5 = ##0xffff */
    [0x0e0 / 4] = 0x9204e5e0, /* weight.b=mxmem(r4,r5) */
    [0x0e4 / 4] = 0x7800ca42, /* r2 = #0x52 */
    [0x0e8 / 4] = 0xa100c200, /* memb(r0+#0x0) = r2 */
    [0x0ec / 4] = 0x00004200, /* wait: immext(#0x8000) */
    [0x0f0 / 4] = 0x7800c002, /* r2 = ##0x8000 */
    [0x0f4 / 4] = 0x9182c003, /* r3 = memw(r2+#0x0) */
    [0x0f8 / 4] = 0x7503c020, /* p0 = cmp.eq(r3,#0x1) */
    [0x0fc / 4] = 0x5c00c018, /* if (p0) jump:nt 0x12c <done> */
    [0x100 / 4] = 0x7503c040, /* p0 = cmp.eq(r3,#0x2) */
    [0x104 / 4] = 0x5c00c004, /* if (p0) jump:nt 0x10c <clobber> */
    [0x108 / 4] = 0x59fffff2, /* jump 0xec <wait> */
    [0x10c / 4] = 0xa6e0c011, /* clobber: mxclracc */
    [0x110 / 4] = 0x7800c862, /* r2 = #0x43 */
    [0x114 / 4] = 0xa100c200, /* memb(r0+#0x0) = r2 */
    [0x118 / 4] = 0x00004200, /* immext(#0x8000) */
    [0x11c / 4] = 0x7800c002, /* r2 = ##0x8000 */
    [0x120 / 4] = 0x7800c003, /* r3 = #0x0 */
    [0x124 / 4] = 0xa182c300, /* memw(r2+#0x0) = r3 */
    [0x128 / 4] = 0x59ffffe2, /* jump 0xec <wait> */
    [0x12c / 4] = 0x000041c0, /* done: immext(#0x7000) */
    [0x130 / 4] = 0x7800c006, /* r6 = ##0x7000 */
    [0x134 / 4] = 0x7800c007, /* r7 = #0x0 */
    [0x138 / 4] = 0xa6e6c704, /* mxmem(r6,r7):after:sat.ub=acc */
    [0x13c / 4] = 0x7800c962, /* r2 = #0x4b */
    [0x140 / 4] = 0xa100c200, /* memb(r0+#0x0) = r2 */
    [0x144 / 4] = 0x5800c000, /* spin: jump 0x144 <spin> */
};

static void wait_serial(const char *path, int index, char expect)
{
    gint64 deadline = g_get_monotonic_time() + 60 * G_USEC_PER_SEC;
    char buf[8] = { 0 };
    int fd = open(path, O_RDONLY);

    g_assert(fd != -1);
    while (pread(fd, buf, index + 1, 0) != index + 1) {
        g_assert(g_get_monotonic_time() < deadline);
        g_usleep(10000);
    }
    close(fd);
    g_assert_cmphex(buf[index], ==, expect);
}

static void test_hmx_snapshot(gconstpointer data)
{
    const char *cpu = data;
    g_autofree char *bios = NULL;
    g_autofree char *serial = NULL;
    g_autofree char *disk = NULL;
    g_autofree uint32_t *image = g_memdup2(hmx_guest, sizeof(hmx_guest));
    g_autofree char *out = NULL;
    QTestState *qts;
    int fd;

    for (size_t i = 0; i < ARRAY_SIZE(hmx_guest); i++) {
        image[i] = GUINT32_TO_LE(image[i]);
    }

    fd = g_file_open_tmp("qtest-hexagon-hmx-bXXXXXX", &bios, NULL);
    g_assert(fd != -1);
    g_assert(write(fd, image, sizeof(hmx_guest)) == sizeof(hmx_guest));
    close(fd);

    fd = g_file_open_tmp("qtest-hexagon-hmx-sXXXXXX", &serial, NULL);
    g_assert(fd != -1);
    close(fd);

    fd = g_file_open_tmp("qtest-hexagon-hmx-dXXXXXX", &disk, NULL);
    g_assert(fd != -1);
    close(fd);
    g_assert(mkimg(disk, "qcow2", 8));

    qts = qtest_initf("-M virt -cpu %s -bios %s -no-shutdown "
                      "-chardev file,id=serial0,path=%s "
                      "-serial chardev:serial0 -accel tcg "
                      "-drive if=none,id=snap,file=%s,format=qcow2",
                      cpu, bios, serial, disk);

    /* The multiply is done. */
    wait_serial(serial, 0, 'R');
    out = qtest_hmp(qts, "savevm hmx");
    g_assert_cmpstr(out, ==, "");

    /* Lose the accumulators... */
    qtest_writel(qts, GUEST_FLAG, 2);
    wait_serial(serial, 1, 'C');

    /* ...and get them back. */
    g_free(out);
    out = qtest_hmp(qts, "loadvm hmx");
    g_assert_cmpstr(out, ==, "");
    qtest_writel(qts, GUEST_FLAG, 1);
    wait_serial(serial, 2, 'K');
    g_assert_cmphex(qtest_readb(qts, GUEST_OUT), ==, EXPECTED_BYTE);

    qtest_quit(qts);
    unlink(bios);
    unlink(serial);
    unlink(disk);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    if (!qtest_has_machine("virt")) {
        g_test_skip("virt machine not available");
        return g_test_run();
    }

    /* v75 is the first revision with HMX; v81 has the XFP and F8 additions. */
    qtest_add_data_func("hexagon-hmx-migrate/snapshot/v75",
                        "v75", test_hmx_snapshot);
    qtest_add_data_func("hexagon-hmx-migrate/snapshot/v81",
                        "v81", test_hmx_snapshot);

    return g_test_run();
}
