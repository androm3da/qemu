#include <hexagon_protos.h>
#include <hexagon_types.h>
#include <stdio.h>

int main(void)
{
	unsigned int out[32] __attribute__((aligned(128)));
	HVX_Vector v = Q6_V_vsplat_R(0x12345678);

	puts("about to store");
	*(HVX_Vector *)out = v;
	printf("out[0]=%08x out[31]=%08x\n", out[0], out[31]);
	return out[0] != 0x12345678;
}
