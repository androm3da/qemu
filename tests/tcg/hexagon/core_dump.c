/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Check the Linux Hexagon core ABI after a child faults in a known packet. */

#include <assert.h>
#include <dirent.h>
#include <elf.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include "core_dump.h"

/* Older libc elf.h headers do not define the Hexagon machine number. */
#ifndef EM_HEXAGON
#define EM_HEXAGON 164
#endif

static void __attribute__((noreturn, noinline)) crash(void)
{
    /* Assembly is needed to leave known architectural register values. */
    asm volatile(
        "{ r1 = #0x120 }\n"
        "{ sa0 = r1 }\n"
        "{ r1 = #0x121 }\n"
        "{ lc0 = r1 }\n"
        "{ r1 = #0x122 }\n"
        "{ sa1 = r1 }\n"
        "{ r1 = #0x123 }\n"
        "{ lc1 = r1 }\n"
        "{ r1 = #0x124 }\n"
        "{ m0 = r1 }\n"
        "{ r1 = #0x125 }\n"
        "{ m1 = r1 }\n"
        "{ r1 = #0 }\n"
        "{ usr = r1 }\n"
        "{ r1 = ##0xa55a00ff }\n"
        "{ p3:0 = r1 }\n"
        "{ r1 = #0x140 }\n"
        "{ gp = r1 }\n"
        "{ r1 = #0x129 }\n"
        "{ ugp = r1 }\n"
        "{ r1 = #0x12d }\n"
        "{ cs0 = r1 }\n"
        "{ r1 = #0x12e }\n"
        "{ cs1 = r1 }\n"
        "{ r0 = #0 }\n"
        "{ r1 = #0x101 }\n"
        "{ r2 = #0x102 }\n"
        "{ r3 = #0x103 }\n"
        "{ r4 = #0x104 }\n"
        "{ r5 = #0x105 }\n"
        ".global core_fault\n"
        "core_fault:\n"
        "{ r0 = memw(r0) }\n"
    );
    __builtin_unreachable();
}

static void check_status(FILE *f, long offset, unsigned size, uintptr_t pc)
{
    uint32_t regs[48];
    uint16_t sig;

    assert(size == 268);
    assert(fseek(f, offset + 12, SEEK_SET) == 0);
    assert(fread(&sig, sizeof(sig), 1, f) == 1);
    assert(sig == SIGSEGV);
    assert(fseek(f, offset + 72, SEEK_SET) == 0);
    assert(fread(regs, sizeof(regs), 1, f) == 1);
    assert(regs[0] == 0);
    for (int i = 1; i <= 5; i++) {
        assert(regs[i] == 0x100 + i);
    }
    for (int i = 32; i <= 37; i++) {
        assert(regs[i] == 0x100 + i);
    }
    assert(regs[38] == 0);           /* USR */
    assert(regs[39] == 0xa55a00ff);  /* packed P3:0 */
    assert(regs[40] == 0x140);       /* GP precedes UGP in the Linux ABI */
    assert(regs[41] == 0x129);
    assert(regs[42] == pc);
    assert(regs[45] == 0x12d);       /* CS0 */
    assert(regs[46] == 0x12e);       /* CS1 */
    assert(regs[47] == 0);          /* padding */
}

static void check_core(const char *path, uintptr_t pc)
{
    FILE *f = fopen(path, "rb");
    Elf32_Ehdr ehdr;
    int found = 0;
    int loads = 0;

    assert(f);
    assert(fread(&ehdr, sizeof(ehdr), 1, f) == 1);
    assert(memcmp(ehdr.e_ident, ELFMAG, SELFMAG) == 0);
    assert(ehdr.e_ident[EI_CLASS] == ELFCLASS32);
    assert(ehdr.e_ident[EI_DATA] == ELFDATA2LSB);
    assert(ehdr.e_type == ET_CORE);
    assert(ehdr.e_machine == EM_HEXAGON);
    assert(ehdr.e_flags == 0x68);
    assert(ehdr.e_phentsize == sizeof(Elf32_Phdr));
    for (int i = 0; i < ehdr.e_phnum; i++) {
        Elf32_Phdr phdr;
        long end, offset;

        assert(fseek(f, ehdr.e_phoff + i * sizeof(phdr), SEEK_SET) == 0);
        assert(fread(&phdr, sizeof(phdr), 1, f) == 1);
        if (phdr.p_type == PT_LOAD && phdr.p_filesz &&
            phdr.p_vaddr <= pc && pc < phdr.p_vaddr + phdr.p_filesz) {
            uint32_t packet;

            offset = phdr.p_offset + pc - phdr.p_vaddr;
            assert(fseek(f, offset, SEEK_SET) == 0);
            assert(fread(&packet, sizeof(packet), 1, f) == 1);
            assert(packet == *(uint32_t *)pc);
            loads++;
        }
        if (phdr.p_type != PT_NOTE) {
            continue;
        }
        offset = phdr.p_offset;
        end = offset + phdr.p_filesz;
        while (offset < end) {
            Elf32_Nhdr note;
            char name[8] = { 0 };

            assert(fseek(f, offset, SEEK_SET) == 0);
            assert(fread(&note, sizeof(note), 1, f) == 1);
            assert(note.n_namesz <= sizeof(name));
            assert(fread(name, note.n_namesz, 1, f) == 1);
            offset += sizeof(note) + ((note.n_namesz + 3) & ~3);
            if (note.n_type == NT_PRSTATUS && strcmp(name, "CORE") == 0) {
                check_status(f, offset, note.n_descsz, pc);
                found++;
            }
            offset += (note.n_descsz + 3) & ~3;
            assert(offset <= end);
        }
    }
    assert(found == 1);
    assert(loads == 1);
    assert(fclose(f) == 0);
}

int main(void)
{
    char tmp[] = "/tmp/hexagon-core-XXXXXX";
    struct rlimit limit;
    struct dirent *entry;
    DIR *dir;
    pid_t child;
    int status, cores = 0;
    uintptr_t pc = (uintptr_t)core_fault;

    assert(getrlimit(RLIMIT_CORE, &limit) == 0);
    limit.rlim_cur = 64 * 1024 * 1024;
    if (limit.rlim_max < limit.rlim_cur) {
        puts("SKIP: hard core size limit is less than 64 MiB");
        return 0;
    }
    assert(mkdtemp(tmp));
    assert(chdir(tmp) == 0);
    child = fork();
    assert(child >= 0);
    if (child == 0) {
        assert(setrlimit(RLIMIT_CORE, &limit) == 0);
        crash();
    }
    assert(waitpid(child, &status, 0) == child);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV);
    dir = opendir(".");
    assert(dir);
    while ((entry = readdir(dir))) {
        if (strncmp(entry->d_name, "qemu_", 5) == 0 &&
            strstr(entry->d_name, ".core")) {
            check_core(entry->d_name, pc);
            assert(unlink(entry->d_name) == 0);
            cores++;
        }
    }
    assert(cores == 1);
    assert(closedir(dir) == 0);
    assert(chdir("/") == 0);
    assert(rmdir(tmp) == 0);
    puts("PASS");
    return 0;
}
