/*
 * Copyright (c) 2004, National ICT Australia
 */
/*
 * Copyright (c) 2007 Open Kernel Labs, Inc. (Copyright Holder).
 * All rights reserved.
 *
 * 1. Redistribution and use of OKL4 (Software) in source and binary
 * forms, with or without modification, are permitted provided that the
 * following conditions are met:
 *
 *     (a) Redistributions of source code must retain this clause 1
 *         (including paragraphs (a), (b) and (c)), clause 2 and clause 3
 *         (Licence Terms) and the above copyright notice.
 *
 *     (b) Redistributions in binary form must reproduce the above
 *         copyright notice and the Licence Terms in the documentation and/or
 *         other materials provided with the distribution.
 *
 *     (c) Redistributions in any form must be accompanied by information on
 *         how to obtain complete source code for:
 *        (i) the Software; and
 *        (ii) all accompanying software that uses (or is intended to
 *        use) the Software whether directly or indirectly.  Such source
 *        code must:
 *        (iii) either be included in the distribution or be available
 *        for no more than the cost of distribution plus a nominal fee;
 *        and
 *        (iv) be licensed by each relevant holder of copyright under
 *        either the Licence Terms (with an appropriate copyright notice)
 *        or the terms of a licence which is approved by the Open Source
 *        Initative.  For an executable file, "complete source code"
 *        means the source code for all modules it contains and includes
 *        associated build and other files reasonably required to produce
 *        the executable.
 *
 * 2. THIS SOFTWARE IS PROVIDED ``AS IS'' AND, TO THE EXTENT PERMITTED BY
 * LAW, ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE, OR NON-INFRINGEMENT, ARE DISCLAIMED.  WHERE ANY WARRANTY IS
 * IMPLIED AND IS PREVENTED BY LAW FROM BEING DISCLAIMED THEN TO THE
 * EXTENT PERMISSIBLE BY LAW: (A) THE WARRANTY IS READ DOWN IN FAVOUR OF
 * THE COPYRIGHT HOLDER (AND, IN THE CASE OF A PARTICIPANT, THAT
 * PARTICIPANT) AND (B) ANY LIMITATIONS PERMITTED BY LAW (INCLUDING AS TO
 * THE EXTENT OF THE WARRANTY AND THE REMEDIES AVAILABLE IN THE EVENT OF
 * BREACH) ARE DEEMED PART OF THIS LICENCE IN A FORM MOST FAVOURABLE TO
 * THE COPYRIGHT HOLDER (AND, IN THE CASE OF A PARTICIPANT, THAT
 * PARTICIPANT). IN THE LICENCE TERMS, "PARTICIPANT" INCLUDES EVERY
 * PERSON WHO HAS CONTRIBUTED TO THE SOFTWARE OR WHO HAS BEEN INVOLVED IN
 * THE DISTRIBUTION OR DISSEMINATION OF THE SOFTWARE.
 *
 * 3. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR ANY OTHER PARTICIPANT BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "../k_r_malloc.h"
#include <stdio.h>
#include <stdint.h>
#include <assert.h>

#include <sos/sos.h>

#define NEW_MALLOC

#ifdef THREAD_SAFE
#include <assert.h>
#include <mutex/mutex.h>
#include <l4/thread.h>
extern struct okl4_mutex malloc_mutex;
#endif /* THREAD_SAFE */

#define NALLOC 0x10000
#define MALLOC_AREA_SIZE 0x100000

#ifndef NEW_MALLOC
static char __malloc_area[MALLOC_AREA_SIZE];
static uintptr_t __malloc_bss = (uintptr_t)&__malloc_area;
static uintptr_t __malloc_top = (uintptr_t)&__malloc_area[MALLOC_AREA_SIZE];

#endif // NEW_MALLOC

Header *_kr_malloc_freep = NULL; // GLOBAL

#define round_up(address, size) ((((address) + (size-1)) & (~(size-1))))

// XXX HACK TO GET AROUND STACK ISSUE WITH sender2kernel
static uintptr_t cp;

/*
 * sbrk equiv
 */
Header *
morecore(unsigned int nu)
{
	uintptr_t nb;
	//uintptr_t cp; XXX HACK TO GET AROUND STACK ISSUE WITH sender2kernel
	Header *up;

	nb = round_up(nu * sizeof(Header), NALLOC);

#ifdef NEW_MALLOC
	if (!moremem(&cp, nb)) {
		return NULL;
	}

#else
	if (__malloc_bss + nb > __malloc_top) {
		return NULL;
	}

	cp = __malloc_bss;
	__malloc_bss += nb;

#endif // NEW_MALLOC

	up = (Header *)cp;
	up->s.size = nb / sizeof(Header);
	free((void *)(up + 1));
	return _kr_malloc_freep;
}
