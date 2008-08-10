/*******************************************************************************
* Filename:    src/arch-alpha/pal.c                                            *
* Description: Elf-loader - ELF file kernel/application bootstraper, source    *
*              file defining the Alpha architectures interface to OSF PAL.     *
* Authors:     Adam 'WeirdArms' Wiggins <awiggins@cse.unsw.edu.au>.            *
* Created:     2004-12-01                                                      *
* Notes:       2004-12-17 - awiggins, This code should really be moved into a  *
*              generic Alpha PAL code library when someone can be bothered.    *
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

#include <stdio.h>
#include "pal.h"

/**********
* Globals *
**********/

/* PCBs need to lay on a single page. */
/* awiggins (2004-12-17): FIX ME! Need to enforce this alignment later! */
struct pcb_struct pcb;

/*************
* Functions. *
*************/

void
pal_swap_to_osf(void)
{
	//uint64_t result;
	uint64_t revision;
	uint64_t *L1 = (uint64_t*)0x200802000UL; /* (1<<33 | 1<<23 | 1<<13) */
	struct hwrpb_struct *hwrpb = INIT_HWRPB;
	//void *vptb = (void*)hwrpb->vptb;
	//struct pcb_struct *pcb_pa =
	//	(struct pcb_struct*)find_pa(vptb, (void*)&pcb);
	struct percpu_struct *percpu =
		(struct percpu_struct*)((uint64_t)hwrpb +  
					hwrpb->cpu_slot_offset +
					hwrpb->cpu_slot_size * hwrpb->primary);

	/* Initialise PCB. */
	pcb.ksp = 0;
	pcb.usp = 0;
	pcb.ptbr = L1[1] >> 32;
	pcb.asn = 0;
	pcb.pcc = 0;
	pcb.unique = 0;
	pcb.flags = 1; /* Floating point enabled, perf monitors disabled. */

	printf("elf-loader:\tSwitching to Tru64 PALcode --- ");

	//result = pal_switch_to_osf(2,      /* Tru64 PALcode identifier. */
	//			   &pcb,   /* Return address, asm sets. */
	//			   pcb_pa, /* Physical address of PCB.  */
	//			   vptb);  /* Virtual page table base.  */

	//if(result) {
	//	printf("failed, code %ld\n", result);
	//	pal_halt();
	//}

	revision = percpu->pal_revision = percpu->palcode_avail[2];

	printf("OK, version %ld.%ld\n",
	       (revision >> 8) & 0xff, revision & 0xff);
}

void*
find_pa(void *vptb, void *vaddr)
{
	uint64_t address = (uint64_t)vaddr;
	uint64_t result;

	result = ((uint64_t *)vptb)[address >> 13];
	result >>= 32;
	result <<= 13;
	result |= address & 0x1fff;

	return (void*)result;
}
