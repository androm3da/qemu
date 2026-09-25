#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>

#define SZ (1 << 20)
static unsigned char pat[SZ], buf[SZ];

static int filecheck(const char *path, const char *what)
{
	int f = open(path, O_RDONLY), bad = 0;
	if (read(f, buf, SZ) != SZ) { printf("cow %s: short read\n", what); return 1; }
	close(f);
	for (int i = 0; i < SZ; i++)
		if (buf[i] != pat[i]) { if (bad < 3) printf("cow %s: file byte %#x changed %02x->%02x\n", what, i, pat[i], buf[i]); bad++; }
	printf("cow %s: file %s (%d bytes changed)\n", what, bad ? "MODIFIED" : "intact", bad);
	return bad != 0;
}

int main(void)
{
	int bad = 0;
	for (int i = 0; i < SZ; i++) pat[i] = (unsigned char)(i * 131 + (i >> 8));
	int f = open("/tmp/cowfile", O_RDWR | O_CREAT | O_TRUNC, 0644);
	write(f, pat, SZ); close(f);

	/* 1: private writable mapping, write, unmap; the file must not change */
	f = open("/tmp/cowfile", O_RDONLY);
	unsigned char *m = mmap(0, SZ, PROT_READ | PROT_WRITE, MAP_PRIVATE, f, 0);
	for (int i = 0; i < SZ; i += 4096) m[i] ^= 0xff;
	int mismatch = 0;
	for (int i = 0; i < SZ; i += 4096) if (m[i] != (unsigned char)(pat[i] ^ 0xff)) mismatch++;
	printf("cow private map readback: %s\n", mismatch ? "BAD" : "ok"); bad += mismatch != 0;
	munmap(m, SZ); close(f);
	bad += filecheck("/tmp/cowfile", "after-private-write");

	/* 2: read first (pages present), then write through a private mapping */
	f = open("/tmp/cowfile", O_RDONLY);
	m = mmap(0, SZ, PROT_READ | PROT_WRITE, MAP_PRIVATE, f, 0);
	volatile unsigned char s = 0; for (int i = 0; i < SZ; i += 4096) s += m[i];
	for (int i = 0; i < SZ; i += 4096) m[i + 1] ^= 0x55;
	munmap(m, SZ); close(f);
	bad += filecheck("/tmp/cowfile", "after-read-then-write");

	/* 3: anonymous memory across fork */
	unsigned char *a = malloc(SZ); memcpy(a, pat, SZ);
	pid_t p = fork();
	if (p == 0) { for (int i = 0; i < SZ; i += 4096) a[i] ^= 0xaa; _exit(0); }
	int st; waitpid(p, &st, 0);
	int ch = 0; for (int i = 0; i < SZ; i++) if (a[i] != pat[i]) ch++;
	printf("cow fork anon: parent %s (%d bytes changed)\n", ch ? "MODIFIED" : "intact", ch); bad += ch != 0;

	/* 4: private mapping of a file, child writes, parent's view */
	f = open("/tmp/cowfile", O_RDONLY);
	m = mmap(0, SZ, PROT_READ | PROT_WRITE, MAP_PRIVATE, f, 0);
	s = 0; for (int i = 0; i < SZ; i += 4096) s += m[i];
	p = fork();
	if (p == 0) { for (int i = 0; i < SZ; i += 4096) m[i + 2] ^= 0x33; _exit(0); }
	waitpid(p, &st, 0);
	ch = 0; for (int i = 0; i < SZ; i++) if (m[i] != pat[i]) ch++;
	printf("cow fork file-private: parent %s (%d bytes changed)\n", ch ? "MODIFIED" : "intact", ch); bad += ch != 0;
	munmap(m, SZ); close(f);
	bad += filecheck("/tmp/cowfile", "after-fork-child-write");


	/* 5: text-relocation pattern: map read-only+exec, touch, mprotect RW, write, restore */
	f = open("/tmp/cowfile", O_RDONLY);
	m = mmap(0, SZ, PROT_READ | PROT_EXEC, MAP_PRIVATE, f, 0);
	s = 0; for (int i = 0; i < SZ; i += 4096) s += m[i];
	mprotect(m, SZ, PROT_READ | PROT_WRITE);
	for (int i = 0; i < SZ; i += 4096) m[i + 3] ^= 0x77;
	mprotect(m, SZ, PROT_READ | PROT_EXEC);
	munmap(m, SZ); close(f);
	bad += filecheck("/tmp/cowfile", "after-mprotect-rw-write");

	/* 6: same, but map read-only (no exec) */
	f = open("/tmp/cowfile", O_RDONLY);
	m = mmap(0, SZ, PROT_READ, MAP_PRIVATE, f, 0);
	s = 0; for (int i = 0; i < SZ; i += 4096) s += m[i];
	mprotect(m, SZ, PROT_READ | PROT_WRITE);
	for (int i = 0; i < SZ; i += 4096) m[i + 4] ^= 0x21;
	munmap(m, SZ); close(f);
	bad += filecheck("/tmp/cowfile", "after-ro-mprotect-write");

	/* 7: write to a page that was never read first */
	f = open("/tmp/cowfile", O_RDONLY);
	m = mmap(0, SZ, PROT_READ, MAP_PRIVATE, f, 0);
	mprotect(m, SZ, PROT_READ | PROT_WRITE);
	for (int i = 0; i < SZ; i += 4096) m[i + 5] ^= 0x42;
	munmap(m, SZ); close(f);
	bad += filecheck("/tmp/cowfile", "after-untouched-mprotect-write");

	printf("cowtest overall: %s\n", bad ? "FAIL" : "PASS");
	return bad != 0;
}
