/*********************************************************************
 *
 * Copyright (C) 2004,  National ICT Australia (NICTA)
 *
 * File path:     platform/ixp4xx/timer.cc
 * Description:   Periodic timer handling
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

#include <kernel/l4.h>
#include <kernel/debug.h>
#include <kernel/platform_support.h>
#include <kernel/plat/timer.h>
#include <kernel/plat/intctrl.h>

#if !defined(CONFIG_PERF)

static inline void hdw_init_timer()
{
    XSCALE_OS_TIMER_STATUS = 0xff;
    XSCALE_OS_TIMER_RELOAD = ((TIMER_PERIOD) & (~2ul)) | 1;
    TRACE("%s: Using external timer 0\n", __FUNCTION__);
}

static inline void hdw_rearm_timer()
{
    XSCALE_OS_TIMER_STATUS = 0x01;  /* Clear timer */
}

#else // CONFIG_PERF

static inline void hdw_rearm_timer()
{
    __asm__(
	"   mrc	    p14, 0, r1, c1, c1, 0	\n"	/* read the amount of overflow	    */
	"   subs    r1,	r1, %0			\n"	/* r1 = -TIMER-PERIOD + overflow    */
	"   mvnpl   r1,	%0			\n"	/* if r1 > 0 r1 = -TIMER-PERIOD	    */
	"   mcr	    p14, 0, r1, c1, c1, 0	\n"	/* Set the timer for the next period */
	"   mrc	    p14, 0, r1, c4, c1, 0	\n"	/* get int enable register	*/
	"   orr	    r1,	r1, #1			\n"	/* Turn on the CCNT bit		*/
	"   mcr	    p14, 0, r1, c5, c1, 0	\n"	/* clear CCNT overflow		*/
	"   mcr	    p14, 0, r1, c4, c1, 0	\n"	/* enable CCNT interrupt	*/
	::  "i" (TIMER_PERIOD) : "r1"
    );
}

static inline void hdw_init_timer()
{
    TRACE("%s: Using internal PMU timer\n", __FUNCTION__);

    // The value of 0xf, enables the CCNT with a 64 divisor, clears all of the
    // performance counters down and enables the interrupt too.
    __asm__(" mcr p14, 0, %0, c0, c1, 0\n" : : "r" (0xf));
    hdw_rearm_timer();
}

#endif // CONFIG_PERF

namespace Platform
{

void NORETURN
handle_timer_interrupt(bool wakeup, continuation_t cont)
{
    //TRACE("%s: rearming timer\n", __FUNCTION__);
    hdw_rearm_timer();
    PlatformSupport::scheduler_handle_timer_interrupt(wakeup, cont);
}

bool ticks_disabled;

void disable_timer_tick(void)
{
    ticks_disabled = true;
    //TRACE("%s: masking IRQ %d\n", __FUNCTION__, XSCALE_IRQ_OS_TIMER);
    ixp4xx_mask(XSCALE_IRQ_OS_TIMER);
    //TRACE("%s: interrupt status = 0x%lx\n", __FUNCTION__, XSCALE_INT_STATUS);
}

word_t init_clocks(void)
{
    hdw_init_timer();
    
    ticks_disabled = false;
    //TRACE("%s: unmasking IRQ %d\n", __FUNCTION__, XSCALE_IRQ_OS_TIMER);
    ixp4xx_unmask(XSCALE_IRQ_OS_TIMER);
    //TRACE("%s: interrupt status = 0x%lx\n", __FUNCTION__, XSCALE_INT_STATUS);
    return TIMER_TICK_LENGTH;
}

}
