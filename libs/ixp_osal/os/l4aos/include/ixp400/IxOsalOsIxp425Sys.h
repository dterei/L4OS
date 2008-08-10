/**
 * @file IxOsalOsIxp425Sys.h
 *
 * @brief l4aos and IXP425 specific defines
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

#ifndef IxOsalOsIxp425Sys_H
#define IxOsalOsIxp425Sys_H

#include "ixp_osal/os/IxOsalOs.h"

#ifndef IxOsalOsIxp400_H
#error "Error: IxOsalOsIxp425Sys.h cannot be included directly before IxOsalOsIxp400.h"
#endif

/* Memory Mapping size */
#define IX_OSAL_IXP400_PERIPHERAL_MAP_SIZE  0x10000	// Map 64k in

/* Time Stamp Resolution */
#define IX_OSAL_IXP400_TIME_STAMP_RESOLUTION    (66666666) /* 66.66MHz */


/*********************
 *	Memory map
 ********************/

/* Note: - dynamic maps will be mapped using ioremap() with the base addresses and sizes declared in this array (matched to include the requested zones, 
           but not using the actual IxOsalMemoryMap requests) 
         - static maps have to be also declared in arch/arm/mach-ixp425/mm.c using the exact same addresses and size
         - the user-friendly name will be made available in /proc for dynamically allocated maps 
         - the declared order of the maps is important if the maps overlap - when a common zone is requested only the
           first usable map will be always chosen */


/* Global memmap only visible to IO MEM module */


#ifdef IxOsalIoMem_C
IxOsalMemoryMap ixOsalGlobalMemoryMap[] = {
    /* Queue Manager */
    {
     IX_OSAL_DYNAMIC_MAP,		/* type            */
     IX_OSAL_IXP400_QMGR_PHYS_BASE,	/* physicalAddress */
     IX_OSAL_IXP400_QMGR_MAP_SIZE,	/* size            */
     0,					/* virtualAddress  */
     IxOsalOsMapMemory,			/* mapFunction     */
     IxOsalOsUnmapMemory,		/* unmapFunction   */
     0,					/* refCount        */
     IX_OSAL_BE | IX_OSAL_LE_DC,	/* endianType      */   
     "qMgr"				/* name            */
     },

    // Flash data
    {
     IX_OSAL_DYNAMIC_MAP,		/* type            */
     NSLU2_FLASH_PHYS_BASE,		/* physicalAddress */
     NSLU2_FLASH_MAP_SIZE,		/* size            */
     0,					/* virtualAddress  */
     IxOsalOsMapMemory,			/* mapFunction     */
     IxOsalOsUnmapMemory,		/* unmapFunction   */
     0,					/* refCount        */
     IX_OSAL_BE,			/* endianType      */
     "Flash Data"			/* name            */
     },

    // Expansion Bus Config Registers
    {
     IX_OSAL_DYNAMIC_MAP,		/* type            */
     IX_OSAL_IXP400_EXP_CFG_PHYS_BASE,	/* physicalAddress */
     IX_OSAL_IXP400_EXP_REG_MAP_SIZE,	/* size            */
     0,					/* virtualAddress  */
     IxOsalOsMapMemory,			/* mapFunction     */
     IxOsalOsUnmapMemory,		/* unmapFunction   */
     0,					/* refCount        */
     IX_OSAL_BE | IX_OSAL_LE_AC,	/* endianType      */
     "Exp Cfg"				/* name            */
     },

    /* APB Peripherals */
    {
     IX_OSAL_DYNAMIC_MAP,			/* type            */
     IX_OSAL_IXP400_PERIPHERAL_PHYS_BASE,	/* physicalAddress */
     IX_OSAL_IXP400_PERIPHERAL_MAP_SIZE,	/* size            */
     0,						/* virtualAddress  */
     IxOsalOsMapMemory,				/* mapFunction     */
     IxOsalOsUnmapMemory,			/* unmapFunction   */
     0,						/* refCount        */
     IX_OSAL_BE | IX_OSAL_LE_AC,		/* endianType      */
     "peripherals"				/* name            */
     },
};

#endif /* IxOsalIoMem_C */

#endif

