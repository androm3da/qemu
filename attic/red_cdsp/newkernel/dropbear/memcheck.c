#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

static unsigned char *slurp(const char *path, long *len)
{
	int f = open(path, O_RDONLY);
	long cap = 1 << 22, n = 0, r;
	unsigned char *b = malloc(cap);
	while ((r = read(f, b + n, cap - n)) > 0) n += r;
	close(f); *len = n; return b;
}

static int diff(const char *tag, unsigned char *a, unsigned char *b, long n)
{
	int d = 0;
	for (long i = 0; i < n; i++)
		if (a[i] != b[i]) {
			if (d < 3) printf("%s off=%ld ref=%02x now=%02x (xor %02x)\n", tag, i, a[i], b[i], a[i] ^ b[i]);
			d++;
		}
	return d;
}

int main(int argc, char **argv)
{
	long n, n2;
	unsigned char *a = slurp(argv[1], &n), *b = malloc(n);
	memcpy(b, a, n);              /* second anonymous reference */
	int rounds = atoi(argv[2]), secs = atoi(argv[3]);
	printf("memcheck start: %ld bytes, %d rounds every %ds\n", n, rounds, secs);
	for (int r = 1; r <= rounds; r++) {
		struct timespec ts = { secs, 0 };
		nanosleep(&ts, 0);
		unsigned char *c = slurp(argv[1], &n2);
		int df = diff("file", a, c, n < n2 ? n : n2);
		int da = diff("anon", a, b, n);
		printf("round %d: file-vs-ref diffs=%d  anon-vs-anon diffs=%d\n", r, df, da);
		free(c);
	}
	return 0;
}
