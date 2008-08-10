/****************************************************************************
 *
 * Copyright (C) 2004,  National ICT Australia (NICTA)
 *
 * File path:	platform/ixp4xx/console.cc
 * Description:	Intel XScale Console
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
 ***************************************************************************/

#include <kernel/l4.h>
#include <kernel/debug.h>

#include <kernel/arch/config.h>
#include <kernel/plat/console.h>
#include <kernel/plat/offsets.h>
#include <kernel/arch/platform.h>
#include <kernel/kdb/console.h>

#define SERIAL_NAME	"serial"

/****************************************************************************
 *
 *    Serial console support
 *
 ****************************************************************************/

struct serial_xscalecon {
    word_t rbr;  /* 0 */
    word_t ier;  /* 4 */
    word_t fcr;  /* 8 */
    word_t lcr;  /* 12 */
    word_t mcr;  /* 16 */
    word_t lsr;  /* 20 */
    word_t msr;  /* 24 */
    word_t scr;  /* 28 */
};

#define thr rbr
#define iir fcr
#define dll rbr
#define dlm ier
#define dlab lcr

#define LSR_DR		0x01	/* Data ready */
#define LSR_OE		0x02	/* Overrun */
#define LSR_PE		0x04	/* Parity error */
#define LSR_FE		0x08	/* Framing error */
#define LSR_BI		0x10	/* Break */
#define LSR_THRE	0x20	/* Xmit holding register empty */
#define LSR_TEMT	0x40	/* Xmitter empty */
#define LSR_ERR		0x80	/* Error */

static volatile struct serial_xscalecon *serial_regs = 0;


void Platform::serial_putc( char c )
{
    if ( serial_regs )
    {
	while (( serial_regs->lsr & LSR_THRE ) == 0 );

	serial_regs->thr = (word_t)c;
	if ( c == '\n' )
	    Platform::serial_putc( '\r' );
    }
}

int Platform::serial_getc( bool can_block )
{
    if ( serial_regs )
    {
	if (( serial_regs->lsr & LSR_DR ) == 0 )
	{
	    if (!can_block)
		return -1;

	    while (( serial_regs->lsr & LSR_DR ) == 0 );
	}
	return serial_regs->rbr;
    }
    return 0;
}

void Platform::serial_init(void) 
{
    serial_regs = (struct serial_xscalecon*)(IODEVICE_VADDR + CONSOLE_OFFSET);
}

