/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Hexagon system guest ELF core dumps. */

#include "qemu/osdep.h"
#include "cpu.h"
#include "elf.h"
#include "system/dump.h"
#include "internal.h"
#include "core-dump.h"

/* Linux include/linux/elfcore.h: struct elf_prstatus for the 32-bit ABI. */
typedef struct HexagonElfPrstatus {
    uint8_t pad1[24];
    uint32_t pr_pid;
    uint8_t pad2[44];
    HexagonUserRegs pr_reg;
    uint32_t pr_fpvalid;
} QEMU_PACKED HexagonElfPrstatus;

QEMU_BUILD_BUG_ON(offsetof(HexagonElfPrstatus, pr_reg) != 72);
QEMU_BUILD_BUG_ON(sizeof(HexagonElfPrstatus) != 268);

typedef struct HexagonElfNote {
    Elf32_Nhdr header;
    char name[8]; /* "CORE" including NUL, padded to a 4-byte boundary. */
    HexagonElfPrstatus prstatus;
} QEMU_PACKED HexagonElfNote;

int hexagon_cpu_write_elf32_note(WriteCoreDumpFunction f, CPUState *cs,
                                 int cpuid, DumpState *s)
{
    HexagonElfNote note = { 0 };
    HexagonUserRegs regs;

    note.header.n_namesz = cpu_to_dump32(s, sizeof("CORE"));
    note.header.n_descsz = cpu_to_dump32(s, sizeof(note.prstatus));
    note.header.n_type = cpu_to_dump32(s, NT_PRSTATUS);
    memcpy(note.name, "CORE", sizeof("CORE"));
    note.prstatus.pr_pid = cpu_to_dump32(s, cpuid);
    hexagon_core_copy_regs(&regs, cpu_env(cs));
    memcpy(&note.prstatus.pr_reg, &regs, sizeof(regs));

    return f(&note, sizeof(note), s);
}

void hexagon_get_dump_info(const CPUState *cs, ArchDumpInfo *info,
                           const GuestPhysBlockList *guest_phys_blocks)
{
    info->d_machine = EM_HEXAGON;
    info->d_class = ELFCLASS32;
    info->d_endian = ELFDATA2LSB;
}

size_t hexagon_get_note_size(const CPUState *cs, int class, int machine,
                             int nr_cpus)
{
    return sizeof(HexagonElfNote) * nr_cpus;
}
