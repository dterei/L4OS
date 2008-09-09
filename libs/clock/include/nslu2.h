/**
 * @file IxOsalOsIxp400.h
 *
 * @brief OS and platform specific definitions 
 *
 * Design Notes:
 *
 * @par
 * IXP400 SW Release version 2.1
 * 
 * -- Copyright Notice --
 * 
 * @par
 * Copyright (c) 2001-2005, Intel Corporation.
 * All rights reserved.
 * 
 * @par
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Intel Corporation nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * 
 * @par
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * 
 * @par
 * -- End of Copyright Notice --
 */

/*
 * Derived from the intel development software
 *
 * Author: 	Godfrey van der Linden
 * Date:	2006-07-18
 */
#ifndef _NSLU2_H
#define _NSLU2_H

/* physical addresses of different hardware segments of the IXP420 platform */
#define IXP4XX_EXP_BUS_DATA			0x50000000
#define IXP4XX_QMNG_BASE			0x60000000
#define IXP4XX_PCI_CONFIG			0xc0000000
#define IXP4XX_EXP_BUS_CONFIG			0xc4000000
#define IXP4XX_PERIPH_BASE			0xc8000000
#define IXP4XX_SDRAM_CONFIG			0xcc000000

#define NSLU2_PERIPHERAL_PHYS_BASE	 IXP4XX_PERIPH_BASE
#define NSLU2_UART1_PHYS_BASE		(IXP4XX_PERIPH_BASE + 0x0000)
#define NSLU2_UART2_PHYS_BASE		(IXP4XX_PERIPH_BASE + 0x1000)
#define NSLU2_PMU_PHYS_BASE		(IXP4XX_PERIPH_BASE + 0x2000)
#define NSLU2_INTC_PHYS_BASE		(IXP4XX_PERIPH_BASE + 0x3000)
#define NSLU2_GPIO_PHYS_BASE		(IXP4XX_PERIPH_BASE + 0x4000)
#define NSLU2_OSTS_PHYS_BASE		(IXP4XX_PERIPH_BASE + 0x5000)
#define NSLU2_NPEA_PHYS_BASE		(IXP4XX_PERIPH_BASE + 0x6000)
#define NSLU2_NPEB_PHYS_BASE		(IXP4XX_PERIPH_BASE + 0x7000)
#define NSLU2_NPEC_PHYS_BASE		(IXP4XX_PERIPH_BASE + 0x8000)
#define NSLU2_ETH_MAC_B0_PHYS_BASE	(IXP4XX_PERIPH_BASE + 0x9000)
#define NSLU2_ETH_MAC_C0_PHYS_BASE	(IXP4XX_PERIPH_BASE + 0xa000)
#define NSLU2_USB_PHYS_BASE		(IXP4XX_PERIPH_BASE + 0xb000)

#define NSLU2_QMGR_PHYS_BASE		IXP4XX_QMNG_BASE
#define NSLU2_EXP_CFG_PHYS_BASE		IXP4XX_EXP_BUS_CONFIG
#define NSLU2_EXP_BUS_REGS_PHYS_BASE	IXP4XX_EXP_BUS_CONFIG
#define NSLU2_PCI_CFG_PHYS_BASE		IXP4XX_PCI_CONFIG

#define NSLU2_SDRAM_CFG_PHYS_BASE	IXP4XX_SDRAM_CONFIG

#define NSLU2_SERCOM_TRAILER		(IXP4XX_EXP_BUS_DATA + 0x40000 - 80)

/* sizes different hardware segments of the IXP420 platform */
#define NSLU2_QMGR_MAP_SIZE        (0x4000)	/* Queue Manager map size */
#define NSLU2_UART1_MAP_SIZE       (0x1000)	/* UART1 map size */
#define NSLU2_UART2_MAP_SIZE       (0x1000)	/* UART2 map size */
#define NSLU2_PMU_MAP_SIZE         (0x1000)	/* PMU map size */
#define NSLU2_OSTS_MAP_SIZE        (0x1000)	/* OS Timers map size */
#define NSLU2_NPEA_MAP_SIZE        (0x1000)	/* NPE A map size */
#define NSLU2_NPEB_MAP_SIZE        (0x1000)	/* NPE B map size */
#define NSLU2_NPEC_MAP_SIZE        (0x1000)	/* NPE C map size */
#define NSLU2_ETH_MAC_B0_MAP_SIZE  (0x1000)	/* MAC Eth NPEB map size */
#define NSLU2_ETH_MAC_C0_MAP_SIZE  (0x1000)	/* MAC Eth NPEC map size */
#define NSLU2_USB_MAP_SIZE         (0x1000)	/* USB map size */
#define NSLU2_GPIO_MAP_SIZE        (0x1000)	/* GPIO map size */
#define NSLU2_EXP_REG_MAP_SIZE	   (0x1000)	/* Exp Bus Config Registers map size */
#define NSLU2_PCI_CFG_MAP_SIZE     (0x1000)	/* PCI Bus Config Registers map size */

/* Interrupts */
#define NSLU2_NPEA_IRQ		(0)
#define NSLU2_NPEB_IRQ		(1)
#define NSLU2_NPEC_IRQ		(2)
#define NSLU2_QM1_IRQ		(3)
#define NSLU2_QM2_IRQ		(4)
#define NSLU2_TIMER0_IRQ	(5)
#define NSLU2_GPIO0_IRQ		(6)
#define NSLU2_GPIO1_IRQ		(7)
#define NSLU2_PCI_INT_IRQ	(8)
#define NSLU2_PCI_DMA1_IRQ	(9)
#define NSLU2_PCI_DMA2_IRQ	(10)
#define NSLU2_TIMER1_IRQ	(11)
#define NSLU2_USB_IRQ		(12)
#define NSLU2_UART2_IRQ		(13)
#define NSLU2_TIMESTAMP_IRQ	(14)
#define NSLU2_UART1_IRQ		(15)
#define NSLU2_WDOG_IRQ		(16)
#define NSLU2_AHB_PMU_IRQ	(17)
#define NSLU2_XSCALE_PMU_IRQ	(18)
#define NSLU2_GPIO2_IRQ		(19)
#define NSLU2_GPIO3_IRQ		(20)
#define NSLU2_GPIO4_IRQ		(21)
#define NSLU2_GPIO5_IRQ		(22)
#define NSLU2_GPIO6_IRQ		(23)
#define NSLU2_GPIO7_IRQ		(24)
#define NSLU2_GPIO8_IRQ		(25)
#define NSLU2_GPIO9_IRQ		(26)
#define NSLU2_GPIO10_IRQ	(27)
#define NSLU2_GPIO11_IRQ	(28)
#define NSLU2_GPIO12_IRQ	(29)
#define NSLU2_SW_INT1_IRQ	(30)
#define NSLU2_SW_INT2_IRQ	(31)

// Bus frequency to microsecond converter routines
#define NSLU2_TICKS2US(ticks)	((ticks) * 50ULL / 3333ULL)
#define NSLU2_US2TICKS(us)	((us) * 3333ULL / 50ULL)

#endif // _NSLU2_H
