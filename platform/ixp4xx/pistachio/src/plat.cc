/********************************************************************* *                
 *
 * Copyright (C) 2004,  National ICT Australia (NICTA)
 *                
 * File path:     platform/ixp4xx/plat.cc
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

#include <kernel/l4.h>
#include <kernel/plat/cache.h>
#include <kernel/plat/console.h>
#include <kernel/plat/offsets.h>
#include <kernel/plat/timer.h>
#include <kernel/plat/intctrl.h>
#include <kernel/plat/interrupt.h>
#include <kernel/cpu/cpu.h>
#include <kernel/arch/platsupport.h>
#include <kernel/arch/platform.h>

namespace Platform {

/*
 * Initialize the platform specific mappings needed
 * to start the kernel.
 * Add other hardware initialization here as well
 */
bool SECTION(".init")
init(int plat_ver, int support_ver)
{
    bool r;
    
    /* Check for API compatibility. */
    if (plat_ver != Platform::API_VERSION ||
        support_ver != PlatformSupport::API_VERSION) {
        return false;
    }
    
    /* Map in the control registers */
    r = PlatformSupport::add_mapping_to_kernel((addr_t)(IODEVICE_VADDR+CONSOLE_OFFSET),
                                               (addr_t)(XSCALE_DEV_PHYS+CONSOLE_OFFSET),
                                               PlatformSupport::size_4k,
                                               PlatformSupport::read_write, true,
					                           PlatformSupport::uncached);
    PLAT_ASSERT(ALWAYS, r == true);
    
    r = PlatformSupport::add_mapping_to_kernel((addr_t)(IODEVICE_VADDR+INTERRUPT_OFFSET),
                                               (addr_t)(XSCALE_DEV_PHYS+INTERRUPT_OFFSET),
                                               PlatformSupport::size_4k,
                                               PlatformSupport::read_write, true,
					                           PlatformSupport::uncached);
    PLAT_ASSERT(ALWAYS, r == true);
    
    r = PlatformSupport::add_mapping_to_kernel((addr_t)(IODEVICE_VADDR+TIMER_OFFSET),
                                               (addr_t)(XSCALE_DEV_PHYS+TIMER_OFFSET),
                                               PlatformSupport::size_4k,
                                               PlatformSupport::read_write, true,
					                           PlatformSupport::uncached);
    PLAT_ASSERT(ALWAYS, r == true);
    
    r = PlatformSupport::add_mapping_to_kernel((addr_t)(IODEVICE_VADDR+GPIO_OFFSET),
                                               (addr_t)(XSCALE_DEV_PHYS+GPIO_OFFSET),
                                               PlatformSupport::size_4k,
                                               PlatformSupport::read_write, true,
					                           PlatformSupport::uncached);
    PLAT_ASSERT(ALWAYS, r == true);
    
    /* Initialize interrupt handling */
    for (word_t i = 0; i < IRQS; i++) {
        irq_mapping[i].handler.init();
        ixp4xx_mask(i);
    }
    bitmap_init(irq_pending, IRQS, false);
    /* set all GPIO interrupts to be falling edge triggered */
    XSCALE_GPIT1R = 033333333;
    XSCALE_GPIT2R = 033333333;
    /* ack all GPIO interrupts */
    XSCALE_GPISR = 0xffff;
    /* ack/disable interrupts for general purpose timers */
    XSCALE_OST_TIM0_RL = 0;
    XSCALE_OST_TIM1_RL = 0;
    XSCALE_OS_TIMER_STATUS = 0xff;
    
    return true;
}

void SECTION(".init")
dcache_attributes(word_t * size, word_t * line_size, word_t * sets, word_t * ways)
{
#if defined(CONFIG_SUBPLAT_IXP420)
    if (size)
        *size = CACHE_SIZE;
    if (line_size)
        *line_size = CACHE_LINE_SIZE;
    if (sets)
        *sets = 32;
    if (ways)
        *ways = 32;
    
#else
#error Unimplemented ixp4xx platform
#endif
}

void SECTION(".init")
icache_attributes(word_t * size, word_t * line_size, word_t * sets, word_t * ways)
{
#if defined(CONFIG_SUBPLAT_IXP420)
    if (size)
        *size = 32*1024;
    if (line_size)
        *line_size = 32;
    if (sets)
        *sets = 32;
    if (ways)
        *ways = 32;
#else
#error Unimplemented ixp4xx platform
#endif
}

}
