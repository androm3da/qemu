/*
 * Included into the QEMU HMX tests (-include) to run them on the CDSP's Linux,
 * where HMX works on VTCM, not on ordinary memory: the tests' buffers are put in
 * a section that is linked at VTCM_VA, and a constructor maps the VTCM there.
 * The UIO driver lets a process open the VTCM only once it has used HVX or HMX,
 * so the constructor executes a vector instruction first.
 */
#ifndef VTCM_SHIM_H
#define VTCM_SHIM_H

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#define VTCM_VA   0x50000000u
#define VTCM_SIZE 0x10000u   /* one page: what the tests' buffers need, and nothing else lives there */

static void __attribute__((constructor)) vtcm_shim_map(void)
{
	int fd;

	asm volatile("v0 = vxor(v0, v0)" ::: "v0");
	fd = open("/dev/uio0", O_RDWR | O_SYNC);
	if (fd < 0) {
		perror("open /dev/uio0");
		exit(2);
	}
	if (mmap((void *)VTCM_VA, VTCM_SIZE, PROT_READ | PROT_WRITE,
		 MAP_SHARED | MAP_FIXED, fd, 0) == MAP_FAILED) {
		perror("mmap vtcm");
		exit(2);
	}
}

#endif
