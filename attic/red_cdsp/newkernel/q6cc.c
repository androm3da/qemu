/*
 * Probe/raise the CDSP core clock from Linux running on the CDSP, following
 * RubikPi-HexagonLinux/arch/hexagon/platform/sm7325/clock_init.c.
 *   q6cc dump   - read the clock controller registers
 *   q6cc juice  - start the Q6 PLL and switch the core clock to it
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define BASE 0x0A340000u
#define SIZE 0x10000u

enum { PLL_MODE = 0x0, PLL_L_VAL = 0x4, PLL_CAL_L_VAL = 0x8, PLL_USER_CTL = 0xc,
       PLL_USER_CTL_U = 0x10, PLL_USER_CTL_U1 = 0x14, PLL_CONFIG_CTL = 0x18,
       PLL_CONFIG_CTL_U = 0x1c, PLL_CONFIG_CTL_U1 = 0x20, PLL_TEST_CTL = 0x24,
       PLL_TEST_CTL_U = 0x28, PLL_TEST_CTL_U1 = 0x2c, PLL_STATUS = 0x30,
       PLL_FREQ_CTL = 0x34, PLL_OPMODE = 0x38, PLL_STATE = 0x3c,
       PLL_ALPHA_VAL = 0x40, CORE_CMD_RCGR = 0x1020, CORE_CFG_RCGR = 0x1024,
       CORE_CBCR = 0x1040 };

static volatile uint32_t *regs;

static uint32_t rd(uint32_t off) { return regs[off / 4]; }
static void bar(void) { asm volatile("barrier" ::: "memory"); }
static void wr(uint32_t off, uint32_t v) { regs[off / 4] = v; bar(); }
static void setb(uint32_t off, uint32_t m) { wr(off, rd(off) | m); }
static void clrb(uint32_t off, uint32_t m) { wr(off, rd(off) & ~m); }

static uint64_t upcycle(void)
{
	uint64_t c;

	asm volatile("%0 = c31:30" : "=r"(c));
	return c;
}

static void mhz(const char *when)
{
	struct timespec a, b;
	uint64_t c0, c1;
	double dt;

	clock_gettime(CLOCK_MONOTONIC, &a);
	c0 = upcycle();
	do {
		clock_gettime(CLOCK_MONOTONIC, &b);
		dt = (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9;
	} while (dt < 0.2);
	c1 = upcycle();
	printf("%s: core %.1f MHz\n", when, (c1 - c0) / dt / 1e6);
}

static void dump(void)
{
	static const struct { const char *n; uint32_t o; } r[] = {
		{"PLL_MODE", 0x0}, {"PLL_L_VAL", 0x4}, {"PLL_CAL_L_VAL", 0x8},
		{"PLL_USER_CTL", 0xc}, {"PLL_USER_CTL_U", 0x10}, {"PLL_USER_CTL_U1", 0x14},
		{"PLL_CONFIG_CTL", 0x18}, {"PLL_CONFIG_CTL_U", 0x1c},
		{"PLL_CONFIG_CTL_U1", 0x20}, {"PLL_STATUS", 0x30}, {"PLL_OPMODE", 0x38},
		{"PLL_ALPHA_VAL", 0x40}, {"CORE_CMD_RCGR", 0x1020},
		{"CORE_CFG_RCGR", 0x1024}, {"CORE_CBCR", 0x1040},
	};
	unsigned i;

	for (i = 0; i < sizeof(r) / sizeof(r[0]); i++) {
		printf("%-18s +%04x = %08x\n", r[i].n, r[i].o, rd(r[i].o));
		fflush(stdout);
	}
}

static int juice(void)
{
	uint32_t v;
	int timeout = 10000;

	wr(PLL_CONFIG_CTL, 0x20485699);
	wr(PLL_CONFIG_CTL_U, 0x00002261);
	wr(PLL_CONFIG_CTL_U1, 0x329A299C);
	wr(PLL_USER_CTL, 0x0);
	wr(PLL_USER_CTL_U, 0x805);
	wr(PLL_USER_CTL_U1, 0x0);
	wr(PLL_TEST_CTL, 0);
	wr(PLL_TEST_CTL_U, 0);
	wr(PLL_TEST_CTL_U1, 0);

	wr(PLL_L_VAL, 0x31);
	wr(PLL_CAL_L_VAL, 0x44);
	wr(PLL_ALPHA_VAL, 0);

	clrb(PLL_MODE, 0x1);
	clrb(PLL_OPMODE, 0x7);
	setb(PLL_MODE, 0x4);
	setb(PLL_OPMODE, 0x1);

	do {
		usleep(100);
		v = rd(PLL_MODE);
	} while (!(v & 0x80000000) && --timeout);
	if (!timeout) {
		printf("PLL failed to lock, PLL_MODE=%08x\n", v);
		return 1;
	}
	printf("PLL locked, PLL_MODE=%08x\n", v);

	setb(PLL_USER_CTL, 0x1);
	setb(PLL_MODE, 0x1);

	v = rd(CORE_CFG_RCGR);
	v &= ~0x71F;
	v |= 0x201;
	wr(CORE_CFG_RCGR, v);
	setb(CORE_CMD_RCGR, 0x1);
	return 0;
}

int main(int argc, char **argv)
{
	int fd = open("/dev/mem", O_RDWR | O_SYNC);
	void *p;

	if (fd < 0 || argc < 2) {
		perror("open /dev/mem");
		return 1;
	}
	p = mmap(NULL, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, BASE);
	if (p == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	regs = p;
	if (!strcmp(argv[1], "dump")) {
		dump();
	} else if (!strcmp(argv[1], "juice")) {
		mhz("before");
		if (juice())
			return 1;
		dump();
		mhz("after");
	}
	return 0;
}
