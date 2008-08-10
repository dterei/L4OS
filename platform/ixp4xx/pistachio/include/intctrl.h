/*********************************************************************
 *                
 * Copyright (C) 2004,  National ICT Australia (NICTA)
 *                
 * File path:     platform/ixp4xx/intctrl.h
 * Description:   
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
 ********************************************************************/
#ifndef __PLATFORM__IXP4XX__INTCTRL_H__
#define __PLATFORM__IXP4XX__INTCTRL_H__

#include <kernel/arch/intctrl.h>
#include <kernel/arch/hwspace.h>
#include <kernel/arch/thread.h>
#include <kernel/space.h>
#include <kernel/plat/timer.h>
#include <kernel/cpu/cpu.h>

#if defined (CONFIG_SUBPLAT_IXP420)
#define IRQS			32
#else
#warning support not implemented for ixpXXX
#endif

/* Interrupt Controller */
#define INTERRUPT_OFFSET	0x3000

#if !defined(CONFIG_PERF)
#define XSCALE_IRQ_OS_TIMER	 5	// External Timer 0
#else
#define XSCALE_IRQ_OS_TIMER	18	// PMU Timer
#endif

#define XSCALE_INT		(IODEVICE_VADDR + INTERRUPT_OFFSET)

#define XSCALE_INT_STATUS	(*(volatile word_t *)(XSCALE_INT + 0x00))
#define XSCALE_INT_ENABLE	(*(volatile word_t *)(XSCALE_INT + 0x04))
#define XSCALE_INT_SELECT	(*(volatile word_t *)(XSCALE_INT + 0x08))
#define XSCALE_INT_IRQ_STATUS	(*(volatile word_t *)(XSCALE_INT + 0x0c))
#define XSCALE_INT_FIQ_STATUS	(*(volatile word_t *)(XSCALE_INT + 0x10))
#define XSCALE_INT_PRIORITY	(*(volatile word_t *)(XSCALE_INT + 0x14))
#define XSCALE_INT_IRQ_PRIO	(*(volatile word_t *)(XSCALE_INT + 0x18))
#define XSCALE_INT_FIQ_PRIO	(*(volatile word_t *)(XSCALE_INT + 0x1c))

namespace Platform {
void ixp4xx_mask(word_t irq);
bool ixp4xx_unmask(word_t irq);
}

static inline void ixp4xx_disable(word_t irq)
{
	Platform::ixp4xx_mask(irq);
}

static inline bool ixp4xx_enable(word_t irq)
{
	return Platform::ixp4xx_unmask(irq);
}

#endif /*__PLATFORM__IXP4XX__INTCTRL_H__ */
