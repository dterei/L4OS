/*********************************************************************
 *
 * Copyright (C) 2004,  National ICT Australia (NICTA)
 *
 * File path:     platform/ixp4xx/irq.cc
 * Description:   Xscale IPX4xx platform demultiplexing
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
#include <kernel/linear_ptab.h>
#include <kernel/cpu/cpu.h>
#include <kernel/arch/platsupport.h>
#include <kernel/arch/platform.h>
#include <kernel/arch/special.h>
#include <kernel/arch/thread.h>
#include <kernel/plat/offsets.h>
#include <kernel/plat/intctrl.h>
#include <kernel/plat/interrupt.h>
#include <kernel/plat/timer.h>

/* performance counters */
#if defined(CONFIG_PERF)
extern word_t count_CCNT_overflow;
extern word_t count_PMN0_overflow;
extern word_t count_PMN1_overflow;
#endif

namespace Platform {

irq_mapping_t irq_mapping[IRQS];
irq_owner_t   irq_owners[IRQS];
bitmap_t      irq_pending[BITMAP_SIZE(IRQS)];

word_t irq_enable = 0;

void ixp4xx_mask(word_t irq)
{
	ASSERT(DEBUG, irq < IRQS);
	irq_enable &= ~(1ul << irq);
	XSCALE_INT_ENABLE = irq_enable;
}

bool ixp4xx_unmask(word_t irq)
{
	ASSERT(DEBUG, irq < IRQS);
	irq_enable |= (1ul << irq);
	XSCALE_INT_ENABLE = irq_enable;
	return false;
}

bool ixp4xx_do_irq(word_t irq, continuation_t cont)
{
    void *handler = irq_mapping[irq].handler.get_tcb();
    word_t mask = irq_mapping[irq].notify_mask;
    
    //printf("%s: interrupt status = 0x%lx\n", __FUNCTION__, XSCALE_INT_STATUS);
    ixp4xx_mask(irq);

    if (EXPECT_TRUE(handler)) {
        word_t *irq_desc = &((tcb_t*)handler)->get_utcb()->platform_reserved[0];

        //printf("irq %d, handler: %p : %08lx\n", irq, handler, mask);
        if (EXPECT_TRUE(*irq_desc == ~0UL)) {
            *irq_desc = irq;
            return PlatformSupport::deliver_notify(handler, mask, cont);
        } else {
            printf(" - mark pending\n");
            bitmap_set(irq_pending, irq);
        }
    } else {
        printf("spurious? mask %d\n", irq);
    }
    return false;
}


word_t ixp4xx_get_number_irqs(void)
{
	return IRQS;
}

void disable_fiq(void)
{
	XSCALE_INT_SELECT = 0x00; /* No FIQs for now */
	//PLAT_TRACE("%s: interrupt status = 0x%lx\n", __FUNCTION__, XSCALE_INT_STATUS);
	//PLAT_TRACE("%s: GPIO interrupt status = 0x%lx\n", __FUNCTION__, XSCALE_GPISR);
}

bool is_irq_available(int irq)
{
	return (irq >= 0) && (irq < IRQS) && (irq != XSCALE_IRQ_OS_TIMER);
}

/**
 * Configure access controls for interrupts
 */
word_t security_control_interrupt(irq_desc_t *desc, void *owner, word_t control)
{
    word_t irq, *irq_desc;
    //printf("plat: sec control: %p, owner %p, ctrl %lx\n",
    //        desc, owner, control);

    irq_desc = (word_t*)desc;

    irq = *irq_desc;

    //printf(" - sec: irq %d\n", irq);
    if (irq >= IRQS) {
        return EINVALID_PARAM;
    }

    switch (control >> 16) {
    case 0: // grant
        if (irq_owners[irq].owner) {
            return EINVALID_PARAM;
        }
        //printf(" - grant\n");
        irq_owners[irq].owner = owner;
        break;
    case 1: // revoke
        if (irq_owners[irq].owner != owner) {
            return EINVALID_PARAM;
        }
        printf(" - revoke\n");
        irq_owners[irq].owner = NULL;
        break;
    default:
        return EINVALID_PARAM;
    }

    return 0;
}

/**
 * Acknowledge, register and unregister interrupts
 */
word_t config_interrupt(irq_desc_t *desc, void *handler,
        irq_control_t control, void *utcb)
{
    word_t irq, *irq_desc;
    //printf("plat: config interrupt: %p, handler %p, ctrl %lx, utcb: %p\n",
    //        desc, handler, control.get_raw(), utcb);

    irq_desc = (word_t*)desc;

    irq = *irq_desc;

    //printf(" - conf: irq %d\n", irq);
    if (irq >= IRQS) {
        return EINVALID_PARAM;
        printf("%s: invalid irq\n", __FUNCTION__);
    }

    switch (control.get_op()) {
    case irq_control_t::op_register:
        {
            void *owner = (void*)((tcb_t*)handler)->get_space();

            if (irq_owners[irq].owner != owner) {
                printf("%s: return ENO_PRIVILEGE\n", __FUNCTION__);
                return ENO_PRIVILEGE;
            }
            if (irq_mapping[irq].handler.get_tcb()) {
                printf("%s: return EINVALID_PARAM\n", __FUNCTION__);
                return EINVALID_PARAM;
            }
    //        printf(" - reg\n");
            irq_mapping[irq].handler.set_thread_cap((tcb_t *)handler);
            cap_reference_t::add_reference((tcb_t *)handler, &irq_mapping[irq].handler);
            irq_mapping[irq].notify_mask = (1UL << control.get_notify_bit());

            /* XXX this is bad */
            if (((tcb_t*)handler)->get_utcb()->platform_reserved[0] == 0) {
    //            printf(" - init\n");
                ((tcb_t*)handler)->get_utcb()->platform_reserved[0] = ~0UL;
            }

            ixp4xx_unmask(irq);
        }
        break;
    case irq_control_t::op_unregister:
        {
            void *owner = (void*)((tcb_t*)handler)->get_space();

            if (irq_owners[irq].owner != owner) {
                printf("%s: return ENO_PRIVILEGE\n", __FUNCTION__);
                return ENO_PRIVILEGE;
            }
            if (irq_mapping[irq].handler.get_tcb() != handler) {
                printf("%s: return EINVALID_PARAM\n", __FUNCTION__);
                return EINVALID_PARAM;
            }
            printf(" - unreg\n");
            cap_reference_t::remove_reference((tcb_t *)handler, &irq_mapping[irq].handler);
            irq_mapping[irq].notify_mask = 0;

            /* XXX this is bad */
            ((tcb_t*)handler)->get_utcb()->platform_reserved[0] = ~0UL;

            ixp4xx_mask(irq);
        }
        break;
    case irq_control_t::op_ack:
    case irq_control_t::op_ack_wait:
        {
            int pend;
            word_t i;

            bitmap_t tmp_pending[BITMAP_SIZE(IRQS)];

    //        printf(" - ack\n");
            if (irq_mapping[irq].handler.get_tcb() != handler) {
                printf("%s: return ENO_PRIVILEGE\n", __FUNCTION__);
                return ENO_PRIVILEGE;
            }

            // XXX checks here
            ((tcb_t*)handler)->get_utcb()->platform_reserved[0] = ~0UL;
            ixp4xx_unmask(irq);

            pend = bitmap_findfirstset(irq_pending, IRQS);

            /* handle pending interrupts for handler */
            if (EXPECT_FALSE(pend != -1)) {
                for (i = 0; i < BITMAP_SIZE(IRQS); i++) {
                    tmp_pending[i] = irq_pending[i];
                }

                do {
                    if (pend != -1) {
                        bitmap_clear(tmp_pending, pend);

                        void *handler = irq_mapping[irq].handler.get_tcb();
                        if (handler == handler) {
                            bitmap_clear(irq_pending, pend);
                            printf(" - pend = %ld\n", pend);

                            (void)ixp4xx_do_irq(pend, NULL);
                            break;
                        }
                    }
                    pend = bitmap_findfirstset(tmp_pending, IRQS);
                } while (pend != -1);
            }
        }
        break;
    }

    return 0;
}

/**
 * IXP4XX platform interrupt handler
 */
extern "C" NORETURN
void handle_interrupt(word_t arg1, word_t arg2, word_t arg3)
{
    int i;
    continuation_t cont = ASM_CONTINUATION;

    //printf("plat: interrupt\n");
    word_t status = XSCALE_INT_STATUS;
    word_t timer_int = 0, wakeup = 0;
    
    /* Apparently the status bit for an irq (whether masked or not) is *not*
       cleared until the handler acks it with the hardware. So we only
       consider unmasked irqs. */
    status &= irq_enable;
    
//#if defined(CONFIG_PERF)
#if 0
    //Handle pmu_irq here
    if (EXPECT_FALSE(status & (1UL << Platform::PMU_IRQ)))
    {
        bool overflow = false;
        unsigned long FLAG = 0;
        //Read Overflow Flag Register
        __asm__ __volatile__ (
                "   mrc p14, 0, %0, c5, c1, 0   \n"
                :"=r" (FLAG)
                ::
                );
        if (FLAG & (1UL)) //CCNT overflow
        {
            if (count_CCNT_overflow == ~0UL) overflow = true;    
            count_CCNT_overflow++;
        }
        if (FLAG & (1UL << 2)) //PMN1 overflow
        {
            if (count_PMN1_overflow == ~0UL) overflow = true;
            count_PMN1_overflow++;
        }
        if (FLAG & (1UL << 1)) //PMN0 overflow
        {
            if (count_PMN0_overflow == ~0UL) overflow = true;
            count_PMN0_overflow++;
        }
        if ( overflow )
        {
            //overflow cout variable overflows, We do not handle this, leave it to user side.
        }
        else
        {
            // Clear interrupt and continue
            __asm__ __volatile__ (
                    "   mcr p14, 0, %0, c5, c1, 0   \n"
                    :
                    :"r" (FLAG)
                    );
            ACTIVATE_CONTINUATION(cont);
        }
    }
#endif
    
    /* Handle timer interrupt */
    if (status & (1UL << XSCALE_IRQ_OS_TIMER))
    {
        timer_int = 1;
        status &= ~(1UL << XSCALE_IRQ_OS_TIMER);
        if (status == 0) {
            //printf("handling timer interrupt only\n");
            Platform::handle_timer_interrupt(false, cont);
            NOTREACHED();
        }
    }
    
#if defined(CONFIG_SUBPLAT_IXP420)
    if (EXPECT_TRUE(status))
    {
        //printf("handling non-timer interrupt, status = %lx\n", status);
        i = msb(status);
        /* This could be a wakeup from sleep, reenable timer interrupt */
        if (EXPECT_FALSE(ticks_disabled)) {
            //printf("re-enabling ticks\n");
            ticks_disabled = false;
            ixp4xx_unmask(XSCALE_IRQ_OS_TIMER);
        }

        wakeup = ixp4xx_do_irq(i, timer_int ? NULL : cont);
        //printf("wakeup = %d\n", wakeup);
    } else {
        printf("spurious interrupt? status = 0x%lx\n", XSCALE_INT_STATUS);
    }
#else
#error subplatform not implemented.
#endif

    if (timer_int) {
        //printf("handling timer interrupt\n");
        Platform::handle_timer_interrupt(wakeup, cont);
        NOTREACHED();
    }

    if (wakeup) {
        PlatformSupport::schedule(cont);
    } else {
        ACTIVATE_CONTINUATION(cont);
    }
    while (1);
}

}
