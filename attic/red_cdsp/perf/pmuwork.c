/*
 * Workloads with a known instruction mix for the hexagon PMU.  Usage:
 *   pmuwork MODE [N in millions]        (N defaults to 20)
 *  mode 0: N x { load, add } { store }     2 packets, 1 load, 1 store, 1 alu per iteration
 *  mode 1: N x { load } { load }           2 packets, 2 loads
 *  mode 2: N x { store } { store }         2 packets, 2 stores
 *  mode 3: N x { add, add } { add, add }   2 packets, 4 alu
 *  mode 4: N loads, each of a new cache line of a buffer far bigger than the caches
 *  mode 5: N x { r = mpyi } { if (p0) jump } packets with a taken branch per iteration
 */
#include <stdio.h>
#include <stdlib.h>

#define BUF (16 * 1024 * 1024)

int main(int argc, char **argv)
{
	int mode = argc > 1 ? atoi(argv[1]) : 0;
	unsigned long n = (argc > 2 ? strtoul(argv[2], NULL, 0) : 20) * 1000000UL;
	static volatile unsigned int cell[64];
	unsigned int *p = (unsigned int *)cell;
	unsigned int a = 0, b = 0, t = 0, u = 0;

	switch (mode) {
	case 0:
		asm volatile("loop0(1f, %4)\n1:\n"
			     "{ %2 = memw(%3)\n  %0 = add(%0, #1) }\n"
			     "{ memw(%3) = %0 }:endloop0\n"
			     : "+r"(a), "+r"(b), "=&r"(t) : "r"(p), "r"(n)
			     : "memory", "lc0", "sa0");
		break;
	case 1:
		asm volatile("loop0(1f, %3)\n1:\n"
			     "{ %0 = memw(%2) }\n"
			     "{ %1 = memw(%2+#4) }:endloop0\n"
			     : "=&r"(t), "=&r"(u) : "r"(p), "r"(n)
			     : "memory", "lc0", "sa0");
		break;
	case 2:
		asm volatile("loop0(1f, %2)\n1:\n"
			     "{ memw(%1) = %0 }\n"
			     "{ memw(%1+#4) = %0 }:endloop0\n"
			     : : "r"(a), "r"(p), "r"(n) : "memory", "lc0", "sa0");
		break;
	case 3:
		asm volatile("loop0(1f, %4)\n1:\n"
			     "{ %0 = add(%0, #1)\n  %1 = add(%1, #1) }\n"
			     "{ %2 = add(%2, #1)\n  %3 = add(%3, #1) }:endloop0\n"
			     : "+r"(a), "+r"(b), "+r"(t), "+r"(u) : "r"(n) : "lc0", "sa0");
		break;
	case 4: {
		volatile char *buf = malloc(BUF);
		unsigned long i, off = 0;

		for (i = 0; i < BUF; i += 4096)
			buf[i] = 1;
		for (i = 0; i < n; i++) {
			t += buf[off];
			off = (off + 96) & (BUF - 1);
		}
		break;
	}
	case 5:
		asm volatile("loop0(1f, %2)\n1:\n"
			     "{ p0 = cmp.eq(%0, %0)\n  %0 = add(%0, #1) }\n"
			     "{ if (p0) jump:t 2f }\n2:\n"
			     "{ nop }:endloop0\n"
			     : "+r"(a) : "r"(b), "r"(n) : "p0", "lc0", "sa0");
		break;
	}
	printf("pmuwork mode %d: %lu iterations (%u %u %u)\n", mode, n, a, t, u);
	return 0;
}
