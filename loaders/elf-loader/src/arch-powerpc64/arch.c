/*******************************************************************************
* Filename:    src/arch-powerpc64/arch.c                                       *
* Description: Elf-loader - ELF file kernel/application bootstraper, source    *
*              file defining the architecture specific functions for the PPC64 *
*              architecture.                                                   *
* Authors:     Carl van Schaik                                                 *
* Created:     2005-06-01                                                      *
********************************************************************************
*                                                                              *
* Australian Public Licence B (OZPLB)                                          *
*                                                                              *
* Version 1-0                                                                  *
*                                                                              *
* Copyright (c) 2004 - 2005 University of New South Wales, Australia           *
*                                                                              *
* All rights reserved.                                                         *
*                                                                              *
* Developed by: Operating Systems, Embedded and                                *
*               Distributed Systems Group (DiSy)                               *
*               University of New South Wales                                  *
*               http://www.disy.cse.unsw.edu.au                                *
*                                                                              *
* Permission is granted by University of New South Wales, free of charge, to   *
* any person obtaining a copy of this software and any associated              *
* documentation files (the "Software") to deal with the Software without       *
* restriction, including (without limitation) the rights to use, copy,         *
* modify, adapt, merge, publish, distribute, communicate to the public,        *
* sublicense, and/or sell, lend or rent out copies of the Software, and        *
* to permit persons to whom the Software is furnished to do so, subject        *
* to the following conditions:                                                 *
*                                                                              *
*     * Redistributions of source code must retain the above copyright         *
*       notice, this list of conditions and the following disclaimers.         *
*                                                                              *
*     * Redistributions in binary form must reproduce the above                *
*       copyright notice, this list of conditions and the following            *
*       disclaimers in the documentation and/or other materials provided       *
*       with the distribution.                                                 *
*                                                                              *
*     * Neither the name of University of New South Wales, nor the names of    *
*        its contributors, may be used to endorse or promote products derived  *
*       from this Software without specific prior written permission.          *
*                                                                              *
* EXCEPT AS EXPRESSLY STATED IN THIS LICENCE AND TO THE FULL EXTENT            *
* PERMITTED BY APPLICABLE LAW, THE SOFTWARE IS PROVIDED "AS-IS", AND           *
* NATIONAL ICT AUSTRALIA AND ITS CONTRIBUTORS MAKE NO REPRESENTATIONS,         *
* WARRANTIES OR CONDITIONS OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING          *
* BUT NOT LIMITED TO ANY REPRESENTATIONS, WARRANTIES OR CONDITIONS             *
* REGARDING THE CONTENTS OR ACCURACY OF THE SOFTWARE, OR OF TITLE,             *
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, NONINFRINGEMENT,          *
* THE ABSENCE OF LATENT OR OTHER DEFECTS, OR THE PRESENCE OR ABSENCE OF        *
* ERRORS, WHETHER OR NOT DISCOVERABLE.                                         *
*                                                                              *
* TO THE FULL EXTENT PERMITTED BY APPLICABLE LAW, IN NO EVENT SHALL            *
* NATIONAL ICT AUSTRALIA OR ITS CONTRIBUTORS BE LIABLE ON ANY LEGAL            *
* THEORY (INCLUDING, WITHOUT LIMITATION, IN AN ACTION OF CONTRACT,             *
* NEGLIGENCE OR OTHERWISE) FOR ANY CLAIM, LOSS, DAMAGES OR OTHER               *
* LIABILITY, INCLUDING (WITHOUT LIMITATION) LOSS OF PRODUCTION OR              *
* OPERATION TIME, LOSS, DAMAGE OR CORRUPTION OF DATA OR RECORDS; OR LOSS       *
* OF ANTICIPATED SAVINGS, OPPORTUNITY, REVENUE, PROFIT OR GOODWILL, OR         *
* OTHER ECONOMIC LOSS; OR ANY SPECIAL, INCIDENTAL, INDIRECT,                   *
* CONSEQUENTIAL, PUNITIVE OR EXEMPLARY DAMAGES, ARISING OUT OF OR IN           *
* CONNECTION WITH THIS LICENCE, THE SOFTWARE OR THE USE OF OR OTHER            *
* DEALINGS WITH THE SOFTWARE, EVEN IF NATIONAL ICT AUSTRALIA OR ITS            *
* CONTRIBUTORS HAVE BEEN ADVISED OF THE POSSIBILITY OF SUCH CLAIM, LOSS,       *
* DAMAGES OR OTHER LIABILITY.                                                  *
*                                                                              *
* If applicable legislation implies representations, warranties, or            *
* conditions, or imposes obligations or liability on University of New South   *
* Wales or one of its contributors in respect of the Software that             *
* cannot be wholly or partly excluded, restricted or modified, the             *
* liability of University of New South Wales or the contributor is limited, to *
* the full extent permitted by the applicable legislation, at its              *
* option, to:                                                                  *
* a.  in the case of goods, any one or more of the following:                  *
* i.  the replacement of the goods or the supply of equivalent goods;          *
* ii.  the repair of the goods;                                                *
* iii. the payment of the cost of replacing the goods or of acquiring          *
*  equivalent goods;                                                           *
* iv.  the payment of the cost of having the goods repaired; or                *
* b.  in the case of services:                                                 *
* i.  the supplying of the services again; or                                  *
* ii.  the payment of the cost of having the services supplied again.          *
*                                                                              *
* The construction, validity and performance of this licence is governed       *
* by the laws in force in New South Wales, Australia.                          *
*                                                                              *
*******************************************************************************/

#include <arch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openfirmware/openfirmware.h>
#include <openfirmware/devicetree.h>
#include <l4/kcp.h>
#include <l4/kip.h>
#include "dit64.h"

/********************
* External symbols. *
********************/

extern of_tree_t device_tree;
extern size_t tree_size;
extern void * firmware_entry;

/************
* Functions *
************/

static void * kip = NULL;


static void add_meminfo(L4_MemoryDesc_t *desc, uintptr_t start, uintptr_t end, int type, int subtype)
{
	desc->x.type = type;
	desc->x.t = subtype;
	desc->x.v = 0;
	desc->x.low = start >> 10;
	desc->x.high = end >> 10;
}

static void size_memory(L4_KernelConfigurationPage_t *kcp)
{
	of_tree_device_t *node = of_tree_first( &device_tree );
	L4_MemoryDesc_t *mdesc;

	char *val;
	of_word_t val_size;

	of_word_t cell_size;
	of_word_t address_size;

	/* Get the #cell-size property */
	if (!of_device_get_prop_byname( node, "#size-cells", &val, &val_size ))
		of_exit( "Can't find #size-cells" );
	if (val_size != sizeof(cell_size))
		of_exit( "Invalid data" );

	cell_size = *(of_word_t *)val;

	/* Try get the #address-cells property */
	if (!of_device_get_prop_byname( node, "#address-cells", &val, &val_size )) {
		address_size = cell_size;
	} else {
		if (val_size != sizeof(address_size))
			of_exit( "Invalid data" );

		address_size = *(of_word_t *)val;
	}

	mdesc = (L4_MemoryDesc_t *)((uintptr_t)kip + kcp->MemoryInfo.MemDescPtr +
			(kcp->MemoryInfo.n * sizeof(L4_MemoryDesc_t)));

	while (1)
	{
		of_tree_reg_range_t *range;
		int i, num;

		node = of_tree_find_next_byname( node, "memory" );
		if (!node) break;

		printf("node = %p\n", node);

		if (!of_device_get_prop_byname( node, "reg", &val, &val_size ))
			of_exit( "Can't find 'reg'\n" );

		num = val_size / (sizeof(of_word_t) * (address_size + cell_size));
		range = (of_tree_reg_range_t*)val;

		for ( i = 0; i < num; i ++ )
		{
			uintptr_t base = 0, size = 0;

			switch (address_size) {
			case 1: // XXX any machines were address_cells = 1, size_cells = 2 ???
				base = range->cell1addr1.base;
				size = range->cell1addr1.size;
				break;
			case 2:
				base = range->cell2addr2.base_lo;
				base += (1ul << sizeof(of_word_t)) * range->cell2addr2.base_hi;

				switch (cell_size) {
				case 1:
					size = range->cell1addr2.size;
					break;
				case 2:
					size = range->cell2addr2.size_lo;
					size += (1ul << sizeof(of_word_t)) * range->cell2addr2.size_hi;
					break;
				default:
					of_exit( "unsupported cell size" );
				}
				break;
			default:
				of_exit( "unsupported address size" );
			}
			range = (of_tree_reg_range_t*)((uintptr_t)range + sizeof(of_word_t) * (address_size + cell_size));

			if (size) {
				add_meminfo(mdesc, base, base + size - 1, L4_ConventionalMemoryType, 0);
				kcp->MemoryInfo.n ++;
				mdesc ++;
			}
		}
	}
}

static void fixup_kip(void)
{
	L4_KernelConfigurationPage_t *kcp = kip;
	L4_MemoryDesc_t *mdesc;

	size_memory(kcp);

	mdesc = (L4_MemoryDesc_t *)((uintptr_t)kip + kcp->MemoryInfo.MemDescPtr +
			(kcp->MemoryInfo.n * sizeof(L4_MemoryDesc_t)));

	printf("elf-loader:\tdevice tree is %p\n", device_tree.head);

	kcp->MemoryInfo.n ++;
	add_meminfo(mdesc, (uintptr_t)device_tree.head, (uintptr_t)device_tree.head + tree_size,
			L4_BootLoaderSpecificMemoryType, 0xf);
}

void
arch_init(void)
{
}

void
arch_start_kernel(void * entry)
{
	/* Make entry a physical address */
	uintptr_t phys_entry = (uintptr_t)entry & 0xffffffff;
	void * of_entry = firmware_entry;

	fixup_kip();

	printf("elf-loader:\tStarting kernel\n");

	__asm__ __volatile__ (
	    "	sync		\n"
	    "	isync		\n"
	    "	mtctr	%0	\n"
	    "	mr	5, %1	\n"
	    "	li	3, 0	\n"
	    "	li	4, 0	\n"
	    "	bctr		\n"
	    :: "r" (phys_entry), "r" (of_entry)
	);
	while(1);
}

void
abort(void)
{
	// Enter openfirmware!!
	of_exit("abort: Game over\n");
	while(1); /* We don't return after this */
}

static void find_kip( struct Elf32_Header *file )
{
	int i;
	for (i = 0; i < elf_getNumProgramHeaders(file); i++)
	{
		uint64_t phys;
		char * src;
		char dit_hdr[] = "DitHdr64";

		phys = elf_getProgramHeaderOffset(file, i) + (uint64_t)file;
		src = (char *)phys;

		if ( !strncmp(src, dit_hdr, strlen(dit_hdr)) )
		{
			Dit_Dhdr64 * dhdr = (Dit_Dhdr64*)phys;
			Dit_Phdr64 * phdr;
			int i;

			phys += sizeof ( Dit_Dhdr64 );

			phdr = (Dit_Phdr64*)phys;

			for ( i = 0; i < dhdr->d_phnum; i ++ )
			{
				if ( phdr->p_flags & DIT_KERNEL )
				{
					/* Get physical address of kip */
					printf("elf-loader:\tkip is %lx\n", phdr->p_magic);

					kip = (void*)(phdr->p_magic & 0xffffffff);
					return;
				}
			}
		}
	}
}

#define MSR_IR      0x20
#define MSR_DR      0x10

int arch_claim_memory(struct Elf32_Header *file)
{
	int i, claim = 0;
	uintptr_t msr;

	find_kip( file );

	__asm__ __volatile__ (
	    "	mfmsr	%0	\n"
	    : "=r" (msr)
	);
	/* If running virtual mode - we must claim mem from OpenFirmware */
	if ( ((msr & MSR_IR) && (msr && MSR_DR)) )
		claim = 1;

	/* Claim memory first if needed */
	for (i = 0; claim && i < elf_getNumProgramHeaders(file); i++)
	{
		uint64_t dest, size;
		of_word_t ret;

		dest = elf_getProgramHeaderPaddr(file, i);
		size = elf_getProgramHeaderMemorySize(file, i);

		ret = of_claim_memory( dest, size, 0 );

		if (ret != dest)
			return -1;
	}

	return 0;
}
