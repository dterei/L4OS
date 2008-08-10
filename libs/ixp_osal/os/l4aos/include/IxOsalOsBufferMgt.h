/**
 * @file IxOsalOsBufferMgt.h 
 *
 * @brief l4aos buffer management module definitions.
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


#ifndef IX_OSAL_OS_BUFFER_MGT_H
#define IX_OSAL_OS_BUFFER_MGT_H

#include <lwip/pbuf.h>

/* 
 * Use the default bufferMgt provided by OSAL framework.
 */
#define IX_OSAL_USE_DEFAULT_BUFFER_MGT

#include "ixp_osal/bufferMgt/IxOsalBufferMgtDefault.h"

/* Define lwip buffer macros to access subfields */
#define pbuf(buf) ((struct pbuf *) (buf))
#define IX_OSAL_OSBUF_MDATA(osBufPtr) ( pbuf(osBufPtr)->payload )

#define IX_OSAL_OSBUF_MLEN(osBufPtr) ( pbuf(osBufPtr)->len )

#define IX_OSAL_OSBUF_VALID(osBufPtr) \
    assert(pbuf(osBufPtr)->tot_len == pbuf(osBufPtr)->len)

/* Conversion utilities for lwip-specific buffers */
#define IX_OSAL_OS_CONVERT_OSBUF_TO_IXPBUF(osBufPtr, ixpBufPtr) do {	\
    IX_OSAL_OSBUF_VALID(osBufPtr);					\
    void *pbuf = (void *) osBufPtr;					\
    IX_OSAL_MBUF *mbuf = (IX_OSAL_MBUF *) ixpBufPtr;			\
    									\
    IX_OSAL_MBUF_OSBUF_PTR(mbuf) = pbuf;				\
    IX_OSAL_MBUF_MDATA(mbuf)     = IX_OSAL_OSBUF_MDATA(pbuf);		\
    IX_OSAL_MBUF_PKT_LEN(mbuf)   = IX_OSAL_OSBUF_MLEN(pbuf);		\
    IX_OSAL_MBUF_MLEN(mbuf)      = IX_OSAL_OSBUF_MLEN(pbuf);		\
} while(0)

#define IX_OSAL_OS_CONVERT_IXPBUF_TO_OSBUF(ixpBufPtr, osBufPtr) do {	\
    IX_OSAL_MBUF *mbuf = (IX_OSAL_MBUF *) ixpBufPtr;			\
    									\
    if (mbuf) {								\
	struct pbuf *pbuf = pbuf(IX_OSAL_MBUF_OSBUF_PTR(mbuf)); \
	osBufPtr = pbuf;						\
    									\
	if (pbuf)							\
	    IX_OSAL_OSBUF_MLEN(pbuf) = IX_OSAL_MBUF_PKT_LEN(pbuf);	\
    }									\
} while(0)

#endif /* #define IX_OSAL_OS_BUFFER_MGT_H */
