/*******************************************************************************
* Filename:    src/arch-alpha/switch.S                                         *
* Description: Elf-loader - ELF file kernel/application bootstraper, source    *
*              file defining the Alpha architectures interface to OSF PAL,     *
*              assembly procedures.                                            *
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

/** awiggins (2005-04-27) These includes are pretty messy, really they
 *  should be sourced from a Alpha PAL interface library and an
 *  Alpha specifiec part of an ASM helper library.
 */
#include <l4/alpha/asm.h>
#include <l4/alpha/pal.h>

/**************
* Procedures. *
**************/

BEGIN_PROC(pal_switch_to_osf)
        .prologue 0
        subq    $30,128,$30
        stq     $26,0($30)
        stq     $1,8($30)       # regs clobbered by swppal
        stq     $2,16($30)
        stq     $3,24($30)
        stq     $4,32($30)
        stq     $5,40($30)
        stq     $6,48($30)
        stq     $7,56($30)
        stq     $8,64($30)
        stq     $9,72($30)
        stq     $10,80($30)
        stq     $11,88($30)
        stq     $12,96($30)
        stq     $13,104($30)
        stq     $14,112($30)
        stq     $15,120($30)

        stq     $30,0($17)      /* save KSP in PCB (a1) */

        bis     $30,$30,$20     /* a4 = KSP */
        br      $17,__do_swppal

        ldq     $26,0($30)
        ldq     $1,8($30)
        ldq     $2,16($30)
        ldq     $3,24($30)
        ldq     $4,32($30)
        ldq     $5,40($30)
        ldq     $6,48($30)
        ldq     $7,56($30)
        ldq     $8,64($30)
        ldq     $9,72($30)
        ldq     $10,80($30)
        ldq     $11,88($30)
        ldq     $12,96($30)
        ldq     $13,104($30)
        ldq     $14,112($30)
        ldq     $15,120($30)
        addq    $30,128,$30
        ret ($26)
__do_swppal:
	call_pal PAL_swppal
END_PROC(pal_switch_to_osf)

BEGIN_PROC(pal_imb)
	call_pal PAL_imb
	ret $31, ($26)
END_PROC(pal_imb)
