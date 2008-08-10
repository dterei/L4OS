/*******************************************************************************
* Filename:    src/arch-alpha/pal.c                                            *
* Description: Elf-loader - ELF file kernel/application bootstraper, header    *
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

#include <stdlib.h>

/**********
* Defines *
**********/

/* Address specified by the Alpha console. */
#define INIT_HWRPB (void*) 0x10000000

/********
* Types *
********/

struct hwrpb_struct {
	uint64_t hwrpb_pa;        /* Physical address of hwrpb.              */
	uint64_t validation;      /* "HWRPB\0\0\0"                           */
	uint64_t revision;        /* hwrpb format revision level.            */
	uint64_t size;            /* Size of the hwrpb.                      */
	uint64_t primary;         /* Primary CPU ID.                         */
	uint64_t page_size;       /* Hardware page size in bytes.            */
        /* awiggins (2004-12-17): Not sure about the order on the next two   */
        /* fields.                                                           */
	uint32_t ext_va_size;     /* See Alpha ARM.                          */
	uint32_t pa_size;         /* Size of physical address space in bits. */
	uint64_t max_asn;         /* Maximum valid asn supported.            */
	uint8_t  ssn[16];         /* System serial number.                   */
	uint64_t sys_type;        /* System type.                            */
	uint64_t sys_variation;   /* System sub-type.                        */
	uint64_t sys_revision;    /* System revision code.                   */
	uint64_t intr_freq;       /* Interval clock interrupt frequency.     */
	uint64_t cycle_freq;      /* Cycle counter frequency.                */
	uint64_t vptb;            /* Virtual base table base.                */
	uint64_t res_arch;        /* Reserved for architecture use.          */
	uint64_t tb_hint_offset;  /* Translation buffer hint block offset.   */
	uint64_t cpu_slots_num;   /* Number of per-cpu slots.                */
	uint64_t cpu_slot_size;   /* Size of each per-CPU slot, in bytes.    */
	uint64_t cpu_slot_offset; /* Offset to the first per-CPU slot.       */
	uint64_t ctbs_num;        /* Number of console terminal blocks.      */
	uint64_t ctb_size;        /* Console terminal block size.            */
	uint64_t ctb_offset;      /* Offset to the first CTB table.          */
	uint64_t memdsc_offset;   /* Memory data descriptor table offset.    */
	uint64_t config_offset;   /* Configuration data table offset.        */
	uint64_t fru_offset;      /* FRU table offset.                       */
	uint64_t save_term_va;    /* Save terminal routine virtual address.  */
	uint64_t save_term_value; /* Save terminal value.                    */
	uint64_t rest_term_va;    /* Restore terminal routine va.            */
	uint64_t rest_term_value; /* Restore terminal value.                 */
	uint64_t restart_va;      /* Restart routine virtual address.        */
	uint64_t restart_value;   /* Restart value.                          */
	uint64_t res_system;      /* Reserved for system software.           */
	uint64_t res_hardware;    /* Reserved for hardware.                  */
	uint64_t checksum;        /* hwrpb checksum.                         */
	uint64_t rxrdy_bitmask;
	uint64_t txrdy_bitmask;
	uint64_t dsr_offset;      /* Dynamic system recongnition data block. */
};

struct pcb_struct {
	uint64_t ksp;
	uint64_t usp;
	uint64_t ptbr;
	uint32_t pcc;
	uint32_t asn;
	uint64_t unique;
	uint64_t flags;
	uint64_t res1;
	uint64_t res2;
};

struct percpu_struct {
	uint64_t hwpcb[16];
	uint64_t flags;
	uint64_t pal_memory_size;
	uint64_t pal_scratch_size;
	uint64_t pal_memory_pa;
	uint64_t pal_scratch_pa;
	uint64_t pal_revision;
	uint64_t processor_type;
	uint64_t processor_variation;
	uint64_t processor_revision;
	uint8_t  processor_serial_num[16];
	uint64_t logout_area_pa;
	uint64_t logout_area_length;
	uint64_t halt_pcbb;
	uint64_t halt_pc;
	uint64_t halt_ps;
	uint64_t halt_args;
	uint64_t halt_ra;
	uint64_t halt_pv;
	uint64_t halt_reason;
	uint64_t res_system;
	uint64_t rxtx_buffer_area[21];
	uint64_t palcode_avail[16];
	uint64_t compatibility;
};

/*******************
* External symbols *
*******************/

/* These symbols are defined in various assembly files. */

/** pal_halt() Calls the PAL to halt the machine, this should drop
 *  back to the console.
 */
extern void pal_halt(void);

/** pal_imb() Calls PAL to execute an instruction memory barrier
 *  (imb), this is used to make sure the ICache is coherent with
 *  memory after you do thinks like copy executable code around.
 */
extern void pal_imb(void);

/** pal_switch_to_osf() Calls PAL to switch an OSF PAL implementation,
 *  if the current PAL isn't OSF PAL.
 */
extern uint64_t pal_switch_to_osf(uint64_t ident, struct pcb_struct *pcb_va,
                                  struct pcb_struct *pcb_pa, void *vptb); 

/***********************
* Function prototypes. *
***********************/

/** find_pa() Find the physical address associated with a virtual one.
 *
 *  vptb    - The base address of the virtual linear page table???
 *  vaddr   - The virtual address to be translated.
 *  @return - The physical address associated with the specified vaddr.
 */
void* find_pa(void *vptb, void *vaddr);

/** pal_swap_to_osf() This function switches the current PAL to an OSF
 *  PAL implementation.
 */
void pal_swap_to_osf(void);

