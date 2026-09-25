#include <stdio.h>
#include <time.h>
#include <stdint.h>

static inline uint64_t upcycle(void)
{
	uint64_t c;

	asm volatile("%0 = c31:30" : "=r"(c));
	return c;
}

int main(void)
{
	struct timespec a, b;
	uint64_t c0, c1;
	volatile unsigned long x = 0;
	unsigned long i;

	clock_gettime(CLOCK_MONOTONIC, &a);
	c0 = upcycle();
	for (i = 0; i < 2000000; i++)
		x += i;
	c1 = upcycle();
	clock_gettime(CLOCK_MONOTONIC, &b);
	double dt = (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9;
	printf("2M loop iterations: %.3f s, %llu cycles => %.1f MHz, %.1f cycles/iter\n",
	       dt, (unsigned long long)(c1 - c0), (c1 - c0) / dt / 1e6,
	       (double)(c1 - c0) / 2e6);
	return 0;
}
