/*********************************************************************
 *                
 * Copyright (C) 2004,  National ICT Australia (NICTA)
 *                
 * File path:     platform/ixp4xx/timer.h
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
#ifndef __PLATFORM__IXP4XX__TIMER_H__
#define __PLATFORM__IXP4XX__TIMER_H__

/* IXP4XX Timer */
#define TIMER_TICK_LENGTH	10000
#define TIMER_OFFSET		0x5000

#define TIMER_MAX_RATE          66660000

/* #define TIMER_PERIOD         (TIMER_MAX_RATE/100) */

/*
 * Since the ixp4xx is a XScale based machine we can access immediates up to a
 * fairly large number so I have rounded the required rate to fit into an ARM
 * immediate.
 */
#if !defined(CONFIG_PERF)
#define TIMER_PERIOD             667648	// ARM immediate slow by 0.15%
#else

/*
 * The PMU timer can run at a rate that is 32 times slower than the extern
 * timers.  Actually it runs twice as fast but I have turned the 64 divisor on.
 */
#define TIMER_PERIOD            20736	// ARM immediate fast by .5%
#endif // CONFIG_PERF



#if !defined(ASSEMBLY)

#include <kernel/cpu/cpu.h>

#define XSCALE_TIMERS		(IODEVICE_VADDR + TIMER_OFFSET)

#if !defined(CONFIG_PERF)
/* GPT 0, on interrupt  */
#define XSCALE_OS_TIMER		(*(volatile word_t *)(XSCALE_TIMERS + 0x04))
#define XSCALE_OS_TIMER_RELOAD	(*(volatile word_t *)(XSCALE_TIMERS + 0x08))
#endif // CONFIG_PERF

#define XSCALE_OST_TIM0_RL      (*(volatile word_t *)(XSCALE_TIMERS + 0x08))
#define XSCALE_OST_TIM1_RL      (*(volatile word_t *)(XSCALE_TIMERS + 0x10))
#define XSCALE_OS_TIMER_STATUS	(*(volatile word_t *)(XSCALE_TIMERS + 0x20))

#define XSCALE_WATCHDOG_TIMER   (*(volatile word_t *)(XSCALE_TIMERS + 0x14))
#define XSCALE_WATCHDOG_EN      (*(volatile word_t *)(XSCALE_TIMERS + 0x18))
#define XSCALE_WATCHDOG_KEY     (*(volatile word_t *)(XSCALE_TIMERS + 0x1c))

#endif

namespace Platform {

    void handle_timer_interrupt(bool wakeup, continuation_t cont) NORETURN;

}

#endif /*__PLATFORM__IXP4XX__TIMER_H__ */
