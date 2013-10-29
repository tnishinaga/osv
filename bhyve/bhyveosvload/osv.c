/*-
 * Copyright (c) 2011 NetApp, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: user/syuu/bhyve_standalone_guest/usr.sbin/bhyveload/bhyveload.c 253922 2013-08-04 01:22:26Z syuu $
 */

/*-
 * Copyright (c) 2011 Google, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: user/syuu/bhyve_standalone_guest/usr.sbin/bhyveload/bhyveload.c 253922 2013-08-04 01:22:26Z syuu $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: user/syuu/bhyve_standalone_guest/usr.sbin/bhyveload/bhyveload.c 253922 2013-08-04 01:22:26Z syuu $");

#include <sys/stat.h>
#include <sys/param.h>

#include <machine/specialreg.h>
#include <machine/pc/bios.h>
#include <machine/vmm.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <elf.h>

#include "userboot.h"

#define ADDR_CMDLINE 0x7e00
#define ADDR_TARGET 0x200000
#define ADDR_MB_INFO 0x1000
#define ADDR_E820DATA 0x1100

struct multiboot_info_type {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
} __attribute__((packed));

#define MSR_EFER        0xc0000080
#define CR4_PAE         0x00000020
#define CR4_PSE         0x00000010
#define CR0_PG          0x80000000
#define	CR0_PE		0x00000001	/* Protected mode Enable */
#define	CR0_NE		0x00000020	/* Numeric Error enable (EX16 vs IRQ13) */

#define PG_V	0x001
#define PG_RW	0x002
#define PG_U	0x004
#define PG_PS	0x080

typedef u_int64_t p4_entry_t;
typedef u_int64_t p3_entry_t;
typedef u_int64_t p2_entry_t;

#define	GUEST_NULL_SEL		0
#define	GUEST_CODE_SEL		1
#define	GUEST_DATA_SEL		2
#define	GUEST_GDTR_LIMIT	(3 * 8 - 1)

int osv_load(struct loader_callbacks *cb, uint64_t mem_size);

static void
setup_stand_gdt(uint64_t *gdtr)
{
	gdtr[GUEST_NULL_SEL] = 0;
	gdtr[GUEST_CODE_SEL] = 0x0020980000000000;
	gdtr[GUEST_DATA_SEL] = 0x0000900000000000;
}

extern int disk_fd;
int
osv_load(struct loader_callbacks *cb, uint64_t mem_size)
{
	int i;
	uint32_t		stack[1024];
	p4_entry_t		PT4[512];
	p3_entry_t		PT3[512];
	p2_entry_t		PT2[512];
	uint64_t		gdtr[3];
	struct multiboot_info_type mb_info;
	struct bios_smap e820data[3];
	char cmdline[0x3f * 512];
	void *target;
	Elf64_Ehdr *elf_image;
	size_t resid;

	bzero(&mb_info, sizeof(mb_info));
	mb_info.cmdline = ADDR_CMDLINE;
	mb_info.mmap_addr = ADDR_E820DATA;
	mb_info.mmap_length = sizeof(e820data);
	if (cb->copyin(NULL, &mb_info, ADDR_MB_INFO, sizeof(mb_info))) {
		perror("copyin");
		return (1);
	}
	cb->setreg(NULL, VM_REG_GUEST_RBX, ADDR_MB_INFO);

	e820data[0].base = 0x0;
	e820data[0].length = 654336;
	e820data[0].type = SMAP_TYPE_MEMORY;
	e820data[1].base = 0x100000;
	e820data[1].length = mem_size - 0x100000;
	e820data[1].type = SMAP_TYPE_MEMORY;
	e820data[2].base = 0;
	e820data[2].length = 0;
	e820data[2].type = 0;
	if (cb->copyin(NULL, e820data, ADDR_E820DATA, sizeof(e820data))) {
		perror("copyin");
		return (1);
	}

	if (cb->diskread(NULL, 0, 1 * 512, cmdline, 63 * 512, &resid)) {
		perror("diskread");
	}
	printf("cmdline=%s\n", cmdline);
	if (cb->copyin(NULL, cmdline, ADDR_CMDLINE, sizeof(cmdline))) {
		perror("copyin");
		return (1);
	}

	target = calloc(1, 0x40 * 512 * 4096);
	if (!target) {
		perror("calloc");
		return (1);
	}
#if 0
	if (cb->diskread(NULL, 0, 0x40 * 512 * 4096, target, 128 * 512, &resid)) {
		perror("diskread");
		return (1);
	}
#endif
	ssize_t siz = pread(disk_fd, target, 0x40 * 512 * 4096, 128 * 512);
	if (siz < 0)
		perror("pread");
	if (cb->copyin(NULL, target, ADDR_TARGET, 0x40 * 512 * 4096)) {
		perror("copyin");
		return (1);
	}
	elf_image = (Elf64_Ehdr *)target;
	printf("ident:%c%c%c type:%x machine:%x version:%x entry:0x%lx\n",
		elf_image->e_ident[0], elf_image->e_ident[1], 
		elf_image->e_ident[2], elf_image->e_type, elf_image->e_machine,
		elf_image->e_version, elf_image->e_entry);
	cb->setreg(NULL, VM_REG_GUEST_RBP, ADDR_TARGET);

	bzero(PT4, PAGE_SIZE);
	bzero(PT3, PAGE_SIZE);
	bzero(PT2, PAGE_SIZE);

	/*
	 * Build a scratch stack at physical 0x1000, page tables:
	 *	PT4 at 0x2000,
	 *	PT3 at 0x3000,
	 *	PT2 at 0x4000,
	 *      gdtr at 0x5000,
	 */

	/*
	 * This is kinda brutal, but every single 1GB VM memory segment
	 * points to the same first 1GB of physical memory.  But it is
	 * more than adequate.
	 */
	for (i = 0; i < 512; i++) {
		/* Each slot of the level 4 pages points to the same level 3 page */
		PT4[i] = (p4_entry_t) 0x3000;
		PT4[i] |= PG_V | PG_RW | PG_U;

		/* Each slot of the level 3 pages points to the same level 2 page */
		PT3[i] = (p3_entry_t) 0x4000;
		PT3[i] |= PG_V | PG_RW | PG_U;

		/* The level 2 page slots are mapped with 2MB pages for 1GB. */
		PT2[i] = i * (2 * 1024 * 1024);
		PT2[i] |= PG_V | PG_RW | PG_PS | PG_U;
	}

#ifdef DEBUG
	printf("Start @ %#llx ...\n", addr);
#endif

	cb->copyin(NULL, stack, 0x1200, sizeof(stack));
	cb->copyin(NULL, PT4, 0x2000, sizeof(PT4));
	cb->copyin(NULL, PT3, 0x3000, sizeof(PT3));
	cb->copyin(NULL, PT2, 0x4000, sizeof(PT2));
	cb->setreg(NULL, 4, 0x1200);

	cb->setmsr(NULL, MSR_EFER, EFER_LMA | EFER_LME);
	cb->setcr(NULL, 4, CR4_PAE | CR4_VMXE);
	cb->setcr(NULL, 3, 0x2000);
	cb->setcr(NULL, 0, CR0_PG | CR0_PE | CR0_NE);

	setup_stand_gdt(gdtr);
	cb->copyin(NULL, gdtr, 0x5000, sizeof(gdtr));
        cb->setgdt(NULL, 0x5000, sizeof(gdtr));

	cb->exec(NULL, elf_image->e_entry);
	return (0);
}

