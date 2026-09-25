/*
 * Check that each process keeps its own HVX vector registers while several of
 * them share the core's HVX contexts and are switched in and out.
 */
#include <hexagon_protos.h>
#include <hexagon_types.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int iter = 2000;
static int nproc = 8;

static int child(int id)
{
	HVX_Vector v[4];
	int i, j, bad = 0;

	for (j = 0; j < 4; j++)
		v[j] = Q6_V_vsplat_R((id << 24) | (j << 16) | 0x1234);
	for (i = 0; i < iter; i++) {
		if ((i & 15) == 0)
			sched_yield();
		if ((i & 255) == 0)
			usleep(1000);
		/* keep the compiler from holding them in memory */
		asm volatile("" : "+v"(v[0]), "+v"(v[1]), "+v"(v[2]), "+v"(v[3]));
		for (j = 0; j < 4; j++) {
			unsigned int expect = (id << 24) | (j << 16) | 0x1234;
			unsigned int got[32] __attribute__((aligned(128)));

			*(HVX_Vector *)got = v[j];
			for (int k = 0; k < 32; k++)
				if (got[k] != expect)
					bad++;
		}
	}
	return bad ? 1 : 0;
}

int main(int argc, char **argv)
{
	int i, status, fail = 0;

	if (argc > 1)
		nproc = atoi(argv[1]);
	if (argc > 2)
		iter = atoi(argv[2]);

	for (i = 0; i < nproc; i++) {
		pid_t p = fork();

		if (p == 0)
			_exit(child(i + 1));
	}
	for (i = 0; i < nproc; i++) {
		wait(&status);
		if (!WIFEXITED(status) || WEXITSTATUS(status))
			fail++;
	}
	printf("hvx_ctx: %s (%d of %d processes failed)\n",
	       fail ? "FAIL" : "PASS", fail, nproc);
	return fail != 0;
}
