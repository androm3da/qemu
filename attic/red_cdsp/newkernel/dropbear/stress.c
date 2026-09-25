#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/syscall.h>

static uint64_t xs(uint64_t x){x^=x<<13;x^=x>>7;x^=x<<17;return x;}

/* A/B: xorshift chain, optionally with a syscall every 'every' steps */
static int compute(long iters, long every, uint64_t want)
{
	uint64_t x = 88172645463325252ULL, sum = 0;
	for (long i = 0; i < iters; i++) {
		x = xs(x); sum += x;
		if (every && (i % every) == 0) syscall(SYS_getpid);
	}
	printf("compute iters=%ld every=%ld sum=%016llx %s\n", iters, every,
	       (unsigned long long)sum, sum == want ? "ok" : "BAD");
	return sum != want;
}

/* C: read a file twice, compare the two passes */
static int readverify(const char *path, int rounds)
{
	static unsigned char a[1 << 16], b[1 << 16];
	int bad = 0;
	for (int r = 0; r < rounds; r++) {
		int f1 = open(path, O_RDONLY), f2 = open(path, O_RDONLY);
		long off = 0; int n1, n2, shown = 0;
		while ((n1 = read(f1, a, sizeof a)) > 0) {
			n2 = read(f2, b, sizeof b);
			if (n1 != n2 || memcmp(a, b, n1)) {
				for (int i = 0; i < n1 && shown < 4; i++)
					if (a[i] != b[i]) {
						printf("readverify mismatch off=%ld a=%02x b=%02x\n", off + i, a[i], b[i]);
						shown++;
					}
				bad++;
			}
			off += n1;
		}
		close(f1); close(f2);
	}
	printf("readverify %s rounds=%d bad=%d %s\n", path, rounds, bad, bad ? "BAD" : "ok");
	return bad;
}

/* D: memory pattern soak */
static int memsoak(size_t mb, int passes)
{
	size_t n = mb << 20; uint32_t *p = malloc(n);
	size_t words = n / 4; int bad = 0, shown = 0;
	if (!p) { puts("memsoak: malloc failed"); return 1; }
	for (size_t i = 0; i < words; i++) p[i] = (uint32_t)(i * 2654435761u);
	for (int k = 0; k < passes; k++)
		for (size_t i = 0; i < words; i++)
			if (p[i] != (uint32_t)(i * 2654435761u)) {
				if (shown++ < 6)
					printf("memsoak pass %d off=%zx got=%08x want=%08x\n", k, i * 4, p[i], (uint32_t)(i * 2654435761u));
				bad++; p[i] = (uint32_t)(i * 2654435761u);
			}
	printf("memsoak %zuMB passes=%d bad=%d %s\n", mb, passes, bad, bad ? "BAD" : "ok");
	return bad;
}

int main(int argc, char **argv)
{
	long iters = argc > 2 ? atol(argv[2]) : 2000000;
	uint64_t want = argc > 3 ? strtoull(argv[3], 0, 16) : 0;
	switch (argv[1][0]) {
	case 'A': return compute(iters, 0, want);
	case 'B': return compute(iters, 16, want);
	case 'C': return readverify(argv[2], atoi(argv[3]));
	case 'D': return memsoak(atoi(argv[2]), atoi(argv[3]));
	}
	return 2;
}
