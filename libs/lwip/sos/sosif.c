/*
 * File: ixp400_eth.c
 *
 * Author: Intel Corporation
 *
 * IXP400 Ethernet Driver for Linux
 *
 * @par
 * -- Intel Copyright Notice --
 * 
 * Copyright (c) 2004-2005  Intel Corporation. All Rights Reserved. 
 *  
 * This software program is licensed subject to the GNU
 * General Public License (GPL). Version 2, June 1991, available at
 * http://www.fsf.org/copyleft/gpl.html
 * 
 * -- End Intel Copyright Notice --
 * 
 */

/*
 * Copyright (c) 2001, Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 *
 * Author: Adam Dunkels <adam@sics.se>
 */

/*
 * sosif.c based upon the intel linux ixp400_eth.c driver
 * and the Swedish ICS's ethernetif.c file
 * Modified from stub for Mungi IDL version by <andrewb@cse.unsw.edu.au>
 *
 * Author:	Godfrey van der Linden
 * Date:	2006-07-06
 */
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "libsos.h"

#include "lwip/netif/etharp.h"
#include "lwip/netif/sosif.h"

#include "lwip/debug.h"
#include "lwip/opt.h"
#include "lwip/pbuf.h"

#if 0
#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/sys.h"
#include "lwip/tcp.h"
#include "lwip/dhcp.h"
#endif

/*
 * Intel IXP400 Software specific header files
 */
#include <ixp400_xscale_sw/IxQMgr.h>
#include <ixp400_xscale_sw/IxEthAcc.h>
#include <ixp400_xscale_sw/IxEthDB.h>
#include <ixp400_xscale_sw/IxEthMii.h>
#include <ixp400_xscale_sw/IxEthNpe.h>
#include <ixp400_xscale_sw/IxNpeDl.h>
#include <ixp400_xscale_sw/IxNpeMh.h>
#include <ixp400_xscale_sw/IxFeatureCtrl.h>
#include <ixp400_xscale_sw/IxVersionId.h>
#include <ixp_osal/IxOsal.h>
#include <ixp400_xscale_sw/IxQueueAssignments.h>

#define verbose 1

/* Define those to better describe your network interface. */
#define IFNAME0 'e'
#define IFNAME1 't'

/* boolean values for PHY link speed, duplex, and autonegotiation */
#define PHY_SPEED_10    0
#define PHY_SPEED_100   1
#define PHY_DUPLEX_HALF 0
#define PHY_DUPLEX_FULL 1
#define PHY_AUTONEG_OFF 0
#define PHY_AUTONEG_ON  1

/* number of packets to prealloc for the Rx pool (per driver instance) */
#define RX_MBUF_POOL_SIZE (128)

/* Maximum number of packets in Tx+TxDone queue */
#define TX_MBUF_POOL_SIZE (256)

// Forward declaration
static err_t lowLevelOutput(struct netif *netif, struct pbuf *p);

#ifdef LINK_STATS
#define STAT_INC(type) (stats.link.(type)++)
#else
#define STAT_INC(type)
#endif /* LINK_STATS */      

/* Private device data */
typedef struct {
    IX_OSAL_MBUF_POOL *fRXPool, *fTXPool;
} SosIfState;

/* Collection of boolean PHY configuration parameters */
typedef struct {
    BOOL speed100;
    BOOL duplexFull;
    BOOL autoNegEnabled;
    BOOL linkMonitor;
} PhyCfg;

/*
 * STATIC VARIABLES
 *
 * This section sets several default values for each port.
 * These may be edited if required.
 */

/* maximum number of ports supported by this driver ixp0, ixp1 ....
 * The default is to configure all ports defined in EthAcc component
 */
// XXX gvdl: Did need to set to 2, thanks Scott for finding this
static const int sNumPorts = 2;


/* 
 * The PHY addresses mapped to Intel IXP400 Software EthAcc ports.
 *
 * These are hardcoded and ordered by increasing ports.
 * Overwriting these values by a PHY discovery is disabled by default but
 * can optionally be enabled if required
 * by passing module param no_phy_scan with a zero value
 *
 * Up to 32 PHYs may be discovered by a phy scan. Addresses
 * of all PHYs found will be stored here, but only the first
 * 2 will be used with the Intel IXP400 Software EthAcc ports.
 *
 * See also the function phyInit() in this file.
 *
 * NOTE: The hardcoded PHY addresses have been verified on
 * the IXDP425 and Coyote (IXP4XX RG) Development platforms.
 * However, they may differ on other platforms.
 */
typedef struct {
    int		    fPhyAddress;
    IxEthAccMacAddr fMACAddr;
} PortConfig;
static PortConfig sPortConfig[] = { { 0 }, { 1 } };

typedef struct {
    PhyCfg          fDefaultPhyCfg;
    UINT32          fNPEImage;
} StaticPortConfig;
static const StaticPortConfig sConstPortConfig[] = {
    {	// Port 0
	{ PHY_SPEED_100, PHY_DUPLEX_FULL, PHY_AUTONEG_ON, TRUE },
	IX_NPEDL_NPEIMAGE_NPEB_ETH,  // Npe firmware for NPEB and port 0
    },
    {	// Port 1
	{ PHY_SPEED_100, PHY_DUPLEX_FULL, PHY_AUTONEG_ON, TRUE },
	IX_NPEDL_NPEIMAGE_NPEC_ETH,  // Npe firmware for NPEB and port 0
    },
};

/*
 * DEBUG UTILITY FUNCTIONS
 */
#ifdef DEBUG_DUMP

static void hex_dump(void *buf, int len)
{
    int i;
    for (i = 0 ; i < len; i++) {
	dprintf(0, "%02x", ((UINT8 *) buf)[i]);
	if (i & 1)
	    dprintf(0, " ");
	if (0xf == (i & 0xf))
	    dprintf(0, "\n");
    }
    dprintf(0, "\n");
}

static void mbuf_dump(char *name, IX_OSAL_MBUF *mbuf)
{
    dprintf(0, "+++++++++++++++++++++++++++\n"
	"%s MBUF dump mbuf=%p, m_data=%p, m_len=%d, len=%d\n",
	name, mbuf, IX_OSAL_MBUF_MDATA(mbuf),
	IX_OSAL_MBUF_MLEN(mbuf), IX_OSAL_MBUF_PKT_LEN(mbuf));
    dprintf(0, ">> mbuf:\n");
    hex_dump(mbuf, sizeof(*mbuf));
    dprintf(0, ">> m_data:\n");
    hex_dump(__va(IX_OSAL_MBUF_MDATA(mbuf)), IX_OSAL_MBUF_MLEN(mbuf));
    dprintf(0, "\n-------------------------\n");
}

static void pbuf_dump(char *name, struct pbuf *pbuf)
{
    dprintf(0, "+++++++++++++++++++++++++++\n"
	"%s PBUF dump pbuf=%p, header=%p, payload=%p, len=%d\n",
	name, pbuf, pbuf->header, pbuf->payload, pbuf->len);
    dprintf(0, ">> data:\n");
    hex_dump(pbuf->payload, pbuf->len);
    dprintf(0, "\n-------------------------\n");
}
#endif

//
// freeExtPBuf():
//
// This function gets called when a packet is freed on the recieve path and
// returns the given packet to the network driver.
//

static void
freeExtPBuf(struct pbuf *p)
{
    struct netif *netif = (struct netif *) p->arg[1];
    IX_OSAL_MBUF *mbuf  = (IX_OSAL_MBUF *) p->arg[2];

    IX_OSAL_MBUF_MLEN(mbuf) = IX_OSAL_MBUF_ALLOCATED_BUFF_LEN(mbuf);
    if (ixEthAccPortRxFreeReplenish(netif->num, mbuf))
	assert(!"ixEthAccPortRxFreeReplenish");

    p->free = NULL;
}

/*
 *  DATAPLANE
 */

/* This callback is called when transmission of the packed is done, and
 * IxEthAcc does not need the buffer anymore. The buffers will be returned to
 * the software queues.
 */
static void txDone(UINT32 unused, IX_OSAL_MBUF *mbuf)
{
    assert(mbuf);

    do {
	struct pbuf *pbuf = IX_OSAL_MBUF_OSBUF_PTR(mbuf);

	if (pbuf && pbuf->free != &freeExtPBuf) {
	    IX_OSAL_MBUF_OSBUF_PTR(mbuf) = NULL;	// disconnect pbuf
	    IX_OSAL_MBUF_POOL_PUT(mbuf);
	}
	// else it is a rxbuffer pbuf_free() will return it

	pbuf_free(pbuf);	// release a pbuf ref

	mbuf = IX_OSAL_MBUF_NEXT_BUFFER_IN_PKT_PTR(mbuf);
    } while (mbuf);
}

/* This callback is called when new packet received from MAC
 * and ready to be transfered up-stack.
 *
 * If this is a valid packet, then new pbuf is allocated and switched
 * with the one in mbuf, which is pushed upstack.
 */
static void rxAvail(UINT32 uNetif, IX_OSAL_MBUF *mbuf, UINT32 unused)
{
    struct netif *netif = (struct netif *) uNetif;
    struct pbuf *p = NULL, *arp = NULL;
    const char *error = NULL;

    //dprintf(0, "%s: start, netif=%lx mbuf=%lx\n", __FUNCTION__, netif, mbuf);
    if (IX_OSAL_MBUF_NEXT_PKT_IN_CHAIN_PTR(mbuf)) {
	// netif_rx queue threshold reached, stop hammering the system or we
	// received an unexpected unsupported chained mbuf
	error = "%s: ethAcc overflow\n";
	STAT_INC(lenerr);
    }
    else if (!netif->state) {
	error = "%s: premature packet\n";
	STAT_INC(err);
    }
    else if (IX_OSAL_MBUF_MLEN(mbuf) != IX_OSAL_MBUF_PKT_LEN(mbuf)) {
	error = "%s: multi-segment dropping\n";
	STAT_INC(err);
    }
    else if (!(p = pbuf_alloc(PBUF_LINK, IX_OSAL_MBUF_MLEN(mbuf), PBUF_EXT))) {
	error = "%s: pbuf_alloc failed\n";
	STAT_INC(memerr);
    }

    if (error) {
	dprintf(1, error, __FUNCTION__);
	goto bail;
    }
    
    //dprintf(0, "%s: grabbing headers from payload\n", __FUNCTION__);
    p->headers = p->payload = IX_OSAL_MBUF_MDATA(mbuf);
    //dprintf(0, "%s: grabbing mbuf len\n", __FUNCTION__);
    p->len = IX_OSAL_MBUF_MLEN(mbuf);
    IX_ACC_DATA_CACHE_INVALIDATE(p->headers, p->len);

#ifdef DEBUG_DUMP
    pbuf_dump("rx", pbuf);
#endif

    /* set up the callback function */
    //dprintf(0, "%s: setting up callback\n", __FUNCTION__);
    p->free = &freeExtPBuf;
    p->arg[1] = netif;
    p->arg[2] = mbuf; mbuf = 0;	// save the mbuf and loose our reference

    STAT_INC(recv);
    
    //dprintf(0, "%s: grabbing payload\n", __FUNCTION__);
    struct eth_hdr *ethhdr = p->payload;
    if (!memcmp(ethhdr->src.addr, netif->hwaddr, IX_IEEE803_MAC_ADDRESS_SIZE)) {
	goto bail;
    }

    switch (htons(ethhdr->type)) {
    case ETHTYPE_IP:
	arp = etharp_ip_input(netif, p);
	pbuf_header(p, -14);	// Advance ethernet header
	(*netif->input)(p, netif);
	break;
    case ETHTYPE_ARP:
	arp = etharp_arp_input(netif, (struct eth_addr *)
		     &netif->hwaddr, p);
	break;
    default:
	dprintf(2, "%s: dropped packet with bogus type\n", __FUNCTION__);
	STAT_INC(proterr);
	goto bail;
    }
    
    if (arp) {
	dprintf(0, "arp is: %p %d\n", arp, htons(ethhdr->type) == ETHTYPE_ARP);
	lowLevelOutput(netif, arp);
	pbuf_free(arp);	// Free the reference taken by lowLevelOutput
    }

    return;

bail:
    //dprintf(0, "%s: bailing\n", __FUNCTION__);
    if (p)
	pbuf_free(p);

    // unchain the buffers, if necessary, then replenish the RxFree q
    while (mbuf) {
	IX_OSAL_MBUF *next = IX_OSAL_MBUF_NEXT_BUFFER_IN_PKT_PTR(mbuf);
	IX_OSAL_MBUF_NEXT_BUFFER_IN_PKT_PTR(mbuf) = NULL;
	IX_OSAL_MBUF_MLEN(mbuf) = IX_OSAL_MBUF_ALLOCATED_BUFF_LEN(mbuf);
	if (ixEthAccPortRxFreeReplenish(netif->num, mbuf))
	    assert(!"ixEthAccPortRxFreeReplenish");
	mbuf = next;
    }
    STAT_INC(drop);
}


/*----------------------------------------------------------------------------*/
/*
 * lowLevelOutput():
 *
 * Should do the actual transmission of the packet. The packet is
 * contained in the pbuf that is passed to the function. This pbuf
 * might be chained.
 *
 */
/*----------------------------------------------------------------------------*/
static err_t
lowLevelOutput(struct netif *netif, struct pbuf *p)
{
    SosIfState *state = netif->state;
    assert(p);

    /* All this does is convert a pbuf into a packetsequence */
    IX_OSAL_MBUF *head = NULL, *last = NULL;

    /* Send the data from the pbuf to the interface, one pbuf at a time.
     * The size of the data in each pbuf is kept in the ->len variable. */
    do {
	if (!p->len)
	    continue; /* skip zero-length pbufs */
	
	// Check for turned around receive packet if so jsut use the mbuf
	// directly
	IX_OSAL_MBUF *mbuf;
	if (p->free == &freeExtPBuf)
	    mbuf = (IX_OSAL_MBUF *) p->arg[2];
	else if ( !(mbuf = IX_OSAL_MBUF_POOL_GET(state->fTXPool)) )
	    goto bail;

	if (!head) {
	    head = mbuf;
	    IX_OSAL_MBUF_PKT_LEN(mbuf) = 0;
	    IX_OSAL_MBUF_NEXT_PKT_IN_CHAIN_PTR(mbuf) = NULL;
	}

	IX_OSAL_MBUF_OSBUF_PTR(mbuf) = p; pbuf_ref(p); // grab a pbuf ref
	IX_ACC_DATA_CACHE_FLUSH(p->payload, p->len);
	IX_ETHACC_NE_FLAGS(mbuf)     = 0;
	IX_OSAL_MBUF_MDATA(mbuf)     = p->payload;
	IX_OSAL_MBUF_MLEN(mbuf)      = p->len;
	IX_OSAL_MBUF_PKT_LEN(head)  += p->len;

	if (last)
	    IX_OSAL_MBUF_NEXT_BUFFER_IN_PKT_PTR(last) = mbuf;
	last = mbuf;
	p = p->next;
    } while (p);

    if (head && !ixEthAccPortTxFrameSubmit
			(netif->num, head, IX_ETH_ACC_TX_DEFAULT_PRIORITY))
    {
	STAT_INC(xmit);
	return ERR_OK;
    }

bail:
    if (head) {
	STAT_INC(memerr);
	STAT_INC(drop);
	txDone(0, head);	// Release the mbuf chain back
    }

    return ERR_MEM;
}

/*----------------------------------------------------------------------------*/
/*
 * sosIfOutput():
 *
 * This function is called by the TCP/IP stack when an IP packet
 * should be sent. It calls the function called lowLevelOutput() to
 * do the actuall transmission of the packet.
 *
 */
/*----------------------------------------------------------------------------*/

static err_t
sosIfOutput(struct netif *netif, struct pbuf *p, struct ip_addr *ipaddr)
{
    struct pbuf *q = etharp_output(netif, ipaddr, p);
    if (q)
	return lowLevelOutput(netif, q);
    else {
	STAT_INC(rterr);
	STAT_INC(drop);
	return ERR_MEM;
    }
}

static IxQMgrDispatcherFuncPtr sDispatcherFunc;
static void irqWrapper(void *arg)
{
    (*sDispatcherFunc)((IxQMgrDispatchGroup) arg);
}


/* Initialize QMgr and bind it's interrupts */
static inline void qmngrInit(void)
{
    /* Initialise Queue Manager */
    dprintf(1, "%s: Initialising Queue Manager...\n", __FUNCTION__);
    if (ixQMgrInit())
	assert(!"ixQMgrInit failed");

    ixQMgrDispatcherLoopGet(&sDispatcherFunc); // Get the dispatcher entrypoint
    if (ixOsalIrqBind(IX_OSAL_IXP400_QM1_IRQ_LVL,
	      &irqWrapper, (void *) IX_QMGR_QUELOW_GROUP))
	assert(!"ixOsalIrqBind failed");
}

static inline void ethAccInit(void)
{
    dprintf(1, "%s: Initialising Ethernet Access...\n", __FUNCTION__);
    /* init and start all NPEs before starting the access layer */
    if (ixNpeMhInitialize(IX_NPEMH_NPEINTERRUPTS_YES))
	assert(!"ixNpeMhInitialize");

    int devCount;
    for (devCount = 0; devCount < sNumPorts; devCount++) {
	/* Initialise and Start NPE */
	if (ixNpeDlNpeInitAndStart(sConstPortConfig[devCount].fNPEImage))
	    assert(!"Error starting NPE port");
    }

    /* initialize the Ethernet Access layer */
    if (ixEthAccInit())
	assert(!"ixEthAccInit");
}

static void phyInit(void)
{
    dprintf(1, "%s: Initialising Ethernet PHYs...\n", __FUNCTION__);

    /* detect the PHYs (ethMii requires the PHYs to be detected) 
     * and provides a maximum number of PHYs to search for.
     */
    BOOL physcan[IXP425_ETH_ACC_MII_MAX_ADDR];
    memset(&physcan[0], false, sizeof(physcan));
    if (ixEthMiiPhyScan(physcan, sizeof(sConstPortConfig)/sizeof(sConstPortConfig[0])))
	assert(!"ixEthMiiPhyScan");

    /* Update the hardcoded values with discovered parameters 
     *
     * This set the following mapping
     *  ixp0 --> first PHY discovered  (lowest address)
     *  ixp1 --> second PHY discovered (next address)
     *  .... and so on
     *
     * If the Phy address and the wiring on the board do not
     * match this mapping, then hardcode the values in the
     * phyAddresses array and use no_phy_scan=1 parameter on 
     * the command line.
     */
    int i, phy_found, num_phys_to_set;
    for (i = 0, phy_found = 0; i < IXP425_ETH_ACC_MII_MAX_ADDR; i++) {
	if (physcan[i]) {
	    sPortConfig[phy_found].fPhyAddress = i;
	    dprintf(1, "%s: Found PHY %d at address %d\n",
		    __FUNCTION__, phy_found, i);
	    if (phy_found < sNumPorts)
		sPortConfig[phy_found].fPhyAddress = i;
	    
	    if (++phy_found == IXP425_ETH_ACC_MII_MAX_ADDR)
		break;
	}
    }

    num_phys_to_set = phy_found;

    /* Reset and Set each phy properties */
    for (i = 0; i < num_phys_to_set; i++) {
	const PhyCfg *curPhy = &(sConstPortConfig[i].fDefaultPhyCfg);
	dprintf(1, "%s: Configuring PHY %d\n", __FUNCTION__ , i);
	if (ixEthMiiPhyReset(sPortConfig[i].fPhyAddress))
	    assert(!"ixEthMiiPhyReset");
	if (ixEthMiiPhyConfig(sPortConfig[i].fPhyAddress, 
		  curPhy->speed100, curPhy->duplexFull, curPhy->autoNegEnabled))
	    assert(!"ixEthMiiPhyConfig");
    }

    /* for each device, display the mapping between the ixp device,
     * the IxEthAcc port, the NPE and the PHY address on MII bus. 
     * Also set the duplex mode of the MAC core depending
     * on the default configuration.
     */
    int devCount;
    for (devCount = 0; 
	 devCount < sNumPorts && devCount <  num_phys_to_set;
	 devCount++)
    {
	IxEthAccPortId portId = devCount;
	char npeId = '?';

	if (portId == IX_ETH_PORT_1)
	    npeId = 'B';
	else if (portId == IX_ETH_PORT_2)
	    npeId = 'C';

	dprintf(1, "%s: ethernet %d using NPE%c and the PHY at address %d\n",
	       __FUNCTION__, devCount, npeId, sPortConfig[portId].fPhyAddress);
    }
}

static inline void mediaCheck(struct netif *netif)
{
    dprintf(1, "%s: Checking Media...\n", __FUNCTION__);
    unsigned phyNum = sPortConfig[netif->num].fPhyAddress;
    BOOL linkUp = 0, speed100, fullDuplex, autonegotiate;
    int retry;

    for (retry = 0; retry < 20; retry++) {
	if (ixEthMiiLinkStatus(phyNum,
			    &linkUp, &speed100, &fullDuplex, &autonegotiate))
	assert(!"ixEthMiiLinkStatus");
	if (linkUp)
	    break;
	else if (!retry)
	    printf("waiting for PHY %d to become ready", netif->num);
	else
	    putchar('.');
	ixOsalSleep(100);	// 100 ms sleep to retry
    }
    assert(linkUp);

    ixEthAccPortDuplexModeSet(netif->num,
	    (fullDuplex)? IX_ETH_ACC_FULL_DUPLEX : IX_ETH_ACC_HALF_DUPLEX);
    puts("");
}

static inline void portInit(struct netif *netif)
{
    dprintf(1, "%s: Initialising Port %d...\n", __FUNCTION__, netif->num);
    SosIfState *state = ixOsalCacheDmaMalloc(sizeof(SosIfState)); 

    netif->name[0] = IFNAME0;
    netif->name[1] = IFNAME1;

    netif->output     = sosIfOutput;
    netif->linkoutput = lowLevelOutput;

    /* Initialize the ethAcc port */
    if (ixEthAccPortInit(netif->num))
        assert(!"ixEthAccPortInit");

    /* initialize RX pool & data memory */
    state->fRXPool = IX_OSAL_MBUF_POOL_INIT(RX_MBUF_POOL_SIZE,
					    IX_ETHACC_RX_MBUF_MIN_SIZE,
				           "IXP400 Ethernet Rx Pool");
    if (!state->fRXPool)
	assert(!"Failed to allocate RX Pool");

    /* initialize TX pool */
    state->fTXPool = IX_OSAL_MBUF_POOL_INIT(TX_MBUF_POOL_SIZE, 0, 
				           "IXP400 Ethernet Tx Pool");
    if (!state->fTXPool)
	assert(!"Failed to allocate TX Pool");

    if (ixEthDBFilteringPortMaximumFrameSizeSet(netif->num, 1600))
	assert(!"ixEthDBFilteringPortMaximumFrameSizeSet");

    /* Defines the unicast MAC address
     *
     * Here is a good place to read a board-specific MAC address
     * from a non-volatile memory, e.g. an external eeprom.
     * 
     * This memcpy uses a default MAC address from this
     * source code.
     */

    /* Set MAC addr in h/w (ethAcc checks for MAC address to be valid) */
    IxEthAccMacAddr *addr = (IxEthAccMacAddr*)&sPortConfig[netif->num].fMACAddr;
    if (ixEthAccPortUnicastMacAddressSet(netif->num, addr))
	assert(!"ixEthAccPortUnicastMacAddressSet");
    memcpy(netif->hwaddr, addr, sizeof(netif->hwaddr));

    mediaCheck(netif); // Verify that a link exists and is up

    // Insert all of the recieve mbufs into the RxQ
    UINT32 prev = ixOsalIrqLock();

    /* set the callback supporting the traffic */
    ixEthAccPortTxDoneCallbackRegister(netif->num, txDone, (UINT32) netif);
    ixEthAccPortRxCallbackRegister(netif->num, rxAvail, (UINT32) netif);

    IX_OSAL_MBUF *mbuf;
    while ( (mbuf = IX_OSAL_MBUF_POOL_GET(state->fRXPool)) ) {
	if (ixEthAccPortRxFreeReplenish(netif->num, mbuf))
	    assert(!"ixEthAccPortRxFreeReplenish");
    }
    ixOsalIrqUnlock(prev);

    ixOsalSleep(10);	// Let the ghost interrupts clear down

    prev = ixOsalIrqLock();
    netif->state = state;	// Mark the netif as active and enable
    if (ixEthAccPortEnable(netif->num))
	assert(!"ixEthAccPortEnable");
    ixOsalIrqUnlock(prev);
}

/*
 * sosIfInit():
 *
 * Should be called at the beginning of the program to set up the network
 * interface.  There is not a whole lot of error handling or unload code in
 * this file.  In general this file will assert if unexpected errors occur.
 * Since we can't unload nor disable very nothing is ever freed.
 */

static bool
sosIfOneTimeInit(void)
{
    ixFeatureCtrlSwConfigurationWrite(IX_FEATURECTRL_ETH_LEARNING, FALSE);

    // Map the flash's SERCOM TRAILER which is the mac address
    uint8_t *macAddr0 = ixOsalIoMemMap(NSLU2_SERCOM_TRAILER, 6, IX_OSAL_BE);
    {
	assert(macAddr0);

	// copy port 0 into both mac address slots
	memcpy(&sPortConfig[0].fMACAddr, macAddr0, 6);
	memcpy(&sPortConfig[1].fMACAddr, macAddr0, 6);
	sPortConfig[1].fMACAddr.macAddress[5]++;  // Increment port 1's address
    }
    ixOsalIoMemUnmap(NSLU2_SERCOM_TRAILER, IX_OSAL_BE);

    qmngrInit();	// initialize the required components for this driver
    ethAccInit();	// Initialise the NPEs and access layer
    phyInit();		// Initialise the PHYs
    return true;	// Finished the init
}

void
sosIfInit(struct netif *netif)
{
    static bool doneInit = false;
    if (!doneInit)
	    doneInit = sosIfOneTimeInit();

    portInit(netif);
}

