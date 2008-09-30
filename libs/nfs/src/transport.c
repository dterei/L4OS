/* transport.c - all the crappy functions that 
    should be hidden at all costs */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <l4/types.h>
#include <l4/ipc.h>

#include "nfs/nfs.h"
#include "nfs/rpc.h"
#include "transport.h"

#include "libsos.h"
#include "constants.h"

// Needs to be called every 100ms by somebody
extern void nfs_timeout(void);

/************************************************************
 *  Debugging defines 
 ***********************************************************/
//#define DEBUG_RPC
#ifdef DEBUG_RPC
#define debug(x...) printf( x )
#else
#define debug(x...)
#endif

#define NFS_LOCAL_PORT 200
#define NFS_MACHINE_NAME "boggo"
#define UDP_SIZE NFS_BUFSIZ


/************************************************************
 *  Queue defines 
 ***********************************************************/
struct rpc_queue;

struct rpc_queue
{
    struct pbuf *pbuf;
    xid_t xid;
    int port;
    int timeout;
    struct rpc_queue *next;
    void (*func) (void *, uintptr_t, struct pbuf *);
    void *callback;
    uintptr_t arg;
};

struct rpc_queue *queue = NULL;

/************************************************************
 *  XID Code 
 ***********************************************************/

static xid_t cur_xid = 100;

static xid_t extract_xid(char *data);

static xid_t
get_xid(void)
{
    return ++cur_xid;
}

xid_t
set_rand_xid(int rand)
{
    /* times it by 1000 to increase
       gaps between close random numbers */
    cur_xid = rand * 1000;
    return cur_xid;
}

/***************************************************************
 *  Buffer handling code                                       *
 ***************************************************************/

/* Add a string to the packet */
void
addstring(struct pbuf* pbuf, char *data)
{
    adddata(pbuf, data, strlen(data));
}

/* Add a fixed sized buffer to the packet */
void
adddata(struct pbuf* pbuf, char *data, int size)
{
    int padded;
    int fill = 0;
    
    padded = size;

    addtobuf(pbuf, (char*) &size, sizeof(int));
    
    addtobuf(pbuf, data, size);

    if (padded % 4) {
	 	addtobuf(pbuf, (char*) &fill, 4 - (padded % 4) );
    }	
}

void
skipstring(struct pbuf *pbuf)
{
    int size;
    
    /* extract the size */
    getfrombuf(pbuf, (char*) &size, sizeof(size));
    
    if (size % 4)
	 	size += 4 - (size % 4);
    pbuf_adv_arg(pbuf, 0, size);
}

void
getstring(struct pbuf *pbuf, char *data, int len)
{
    getdata( pbuf, data, len, 1 );
}

int
getdata(struct pbuf *pbuf, char* data, int len, int null)
{
    int size, padsize;

    assert( len > 0 );

    /* extract the size */
    getfrombuf(pbuf, (char*) &size, sizeof(size));

    padsize = size;

    if (size < len)
	 	len = size;

    /* copy bytes into tmp */
    if (padsize % 4)
		padsize += 4 - (padsize % 4);

    getfrombuf(pbuf, data, len);

    pbuf_adv_arg(pbuf, 0, (padsize - len));

    /* add the null pointer to the name */
    if (null)
		data[ len ] = '\0';

    return len;
}

void *
getpointfrombuf(struct pbuf *pbuf, int len)
{
    void * ret = pbuf->arg[0];
    pbuf_adv_arg(pbuf, 0, len);
    return ret;
}

void
getfrombuf(struct pbuf *pbuf, char* data, int len)
{
    memcpy(data, pbuf->arg[0], len);
    pbuf_adv_arg(pbuf, 0, len);
}

void
addtobuf(struct pbuf *pbuf, char *data, int len)
{
	 assert(pbuf->len >= len);
    memcpy(pbuf->arg[0], data, len);
    pbuf_adv_arg(pbuf, 0, len);
}

void
resetbuf(struct pbuf *pbuf)
{
    pbuf->arg[0] = pbuf->payload;
    pbuf->len = SETBUF_LEN;
}

void
clearbuf(struct pbuf *pbuf)
{
    /* clear the buffer and reset the pos */
    memset(&pbuf->payload, 0, SETBUF_LEN );
    resetbuf(pbuf);
}

/* for synchronous calls */
struct pbuf *
initbuf(int prognum, int vernum, int procnum)
{
    xid_t txid = get_xid();
    int calltype = MSG_CALL;
    call_body bod;
    opaque_auth_t cred, verf;
    int nsize;
    int tval;
    struct pbuf *pbuf;
    
    pbuf = pbuf_alloc(PBUF_TRANSPORT, UDP_SIZE, PBUF_RAM);
    assert(pbuf != NULL);

    pbuf->arg[0] = pbuf->payload;

    debug("Creating txid: %d\n", txid);

    /* add the xid */
    addtobuf(pbuf, (char*) &txid, sizeof(xid_t));

    /* set it to call */
    addtobuf(pbuf, (char*) &calltype, sizeof(int));

    /* add the call body - prog/proc info */
    bod.rpcvers = SRPC_VERSION;
    bod.prog = prognum;  /* that's NFS, dammit */
    bod.vers = vernum;
    bod.proc = procnum;
    
    addtobuf(pbuf, (char*) &bod, sizeof(bod));

    /* work out size of name */
    nsize = strlen(NFS_MACHINE_NAME);

    if(nsize % 4)
	nsize += 4 - (nsize % 4);
    
    /* now add the authentication */
    cred.flavour = AUTH_UNIX;
    cred.size = 5 * sizeof(int) + nsize;

    addtobuf(pbuf, (char*) &cred, sizeof(cred));

    /* add the STAMP field */
    tval = 37; /* FIXME: Magic number! */
    addtobuf(pbuf, (char*) &tval, sizeof(tval));

    /* add machine name */
    addstring(pbuf, NFS_MACHINE_NAME);

    /* add uid */
    tval = 0; /* root */
    addtobuf(pbuf, (char*) &tval, sizeof(tval));

    /* add gid */
    tval = 0; /* root */
    addtobuf(pbuf, (char*) &tval, sizeof(tval));

    /* add gids */
    tval = 0;
    addtobuf(pbuf, (char*) &tval, sizeof(tval));

    verf.flavour = AUTH_NULL;
    verf.size = 0;

    addtobuf(pbuf, (char*) &verf, sizeof(verf));

    return pbuf;

}


/***************************************************************
 *  RPC Call code - This is a bit of a hack. L4 Specific       *
 ***************************************************************/
struct pbuf *call_pbuf;

static void
signal(void *unused, uintptr_t arg, struct pbuf *pbuf)
{
    debug("Signal function called\n");
    L4_Msg_t msg;
    L4_MsgClear(&msg);
    L4_MsgLoad(&msg);

    L4_MsgTag_t tag = L4_Send((L4_ThreadId_t) arg);
    if (L4_IpcFailed(tag))
    {
        L4_Word_t ec = L4_ErrorCode();
        printf("%s: IPC error\n", __FUNCTION__);
        sos_print_error(ec);
        assert(!(ec & 1));
    }
    call_pbuf = pbuf;
}

struct pbuf *
rpc_call(struct pbuf *pbuf, int port)
{
    L4_ThreadId_t from;

    opaque_auth_t auth;
    reply_stat r;

    /* Send the thing */
    rpc_send(pbuf, port, signal, NULL, sos_my_tid().raw);

    /* We wait for a reply */
    L4_Wait(&from);
    pbuf_adv_arg(call_pbuf, 0, 8);

    /* check if it was an accepted reply */
    getfrombuf(call_pbuf, (char*) &r, sizeof(r));

    if(r != MSG_ACCEPTED) {
	debug( "Message NOT accepted (%d)\n", r );

	/* extract error code */
	getfrombuf(call_pbuf, (char*) &r, sizeof(r));
	debug( "Error code %d\n", r );
	
	if(r == 1) {
	    /* get the auth problem */
	    getfrombuf(call_pbuf, (char*) &r, sizeof(r));
	    debug( "auth_stat %d\n", r );
	}
	
	return 0;
    }
    
    /* and the auth data!*/
    getfrombuf(call_pbuf, (char*) &auth, sizeof(auth));

    debug("Got auth data. size is %d\n", auth.size);

    /* check its accept stat */
    getfrombuf(call_pbuf, (char*) &r, sizeof(r));
    
    if( r == SUCCESS )
	return call_pbuf;
    else {
	debug( "reply stat was %d\n", r );
	return NULL;
    }
}


/***************************************************************
 *  Queue function                                             *
 ***************************************************************/

static void
add_to_queue(struct pbuf *pbuf, int port, 
	 void (*func)(void *, uintptr_t, struct pbuf *),
	 void *callback, uintptr_t arg)
{
    /* Need a lock here */
    struct rpc_queue *q_item;
    struct rpc_queue *tmp;
    q_item = malloc(sizeof(struct rpc_queue));
    assert(q_item != NULL);

    q_item->next = NULL;
    q_item->pbuf = pbuf;
    q_item->xid = extract_xid(pbuf->payload);
    q_item->timeout = 0;
    q_item->port = port;
    q_item->func = func;
    q_item->arg = arg;
    q_item->callback = callback;

    if (queue == NULL) {
	/* Add at start of the linked list */
	queue = q_item;
    } else {
	/* Add to end of the linked list */
	for(tmp = queue; tmp->next != NULL; tmp = tmp->next)
	    ;
	tmp->next = q_item;
    }
}

/* Remove item from the queue -- doesn't free the memory */
static struct rpc_queue *
get_from_queue(xid_t xid)
{
    struct rpc_queue *tmp, *last = NULL;

    for (tmp = queue; tmp != NULL && tmp->xid != xid; tmp = tmp->next) {
	last = tmp;
	;
    }
    if (tmp == NULL) {
        return NULL;
    } else if (last == NULL) {
	queue = tmp->next;
    } else {
	last->next = tmp->next;
    }

    return tmp;
}

/* Called when we receive a packet */
static void
my_recv(void *arg, struct udp_pcb *upcb, struct pbuf *p,
    struct ip_addr *addr, u16_t port)
{
    xid_t xid;
    struct rpc_queue *q_item;

    xid = extract_xid(p->payload);

    q_item = get_from_queue(xid);

    debug("Recieved a reply for xid: %u (%d) %p\n", 
	  xid, p->len, q_item);
    if (q_item == NULL)
        return;
    if (q_item->func != NULL) {
        p->arg[0] = p->payload;
	q_item->func(q_item->callback, q_item->arg, p);
    }
    
    /* Free our memory */
    pbuf_free(p);
    pbuf_free(q_item->pbuf);
    free(q_item);
}

/* Send a packet to a specific port. It would be nice if 
   this was support by Lwip */
static err_t
udp_send_to(struct udp_pcb *pcb, int port, struct pbuf *pbuf)
{
    int old_port = pcb->remote_port;
    void *oldpayload = pbuf->payload;
    int old_len = pbuf->len;

    err_t ret;
    pcb->remote_port = port;
    ret = udp_send(pcb, pbuf);

    /* Reset these variables so this packet can be sent
       again. It would be nie if Lwip provided a nice interface
       for resending packets.
    */
    pbuf->payload = oldpayload;
    pbuf->len = pbuf->tot_len = old_len;
 
    /* Reset to the port */
    pcb->remote_port = old_port;
    return ret;
}

struct udp_pcb *udp_cnx;

/* Called every 100ms. Items in the queue are resent every 500ms */
void
nfs_timeout(void)
{
    struct rpc_queue *q_item;

    for (q_item = queue; q_item != NULL; q_item = q_item->next) {
	if (q_item->timeout++ > 5) {
	    udp_send_to(udp_cnx, q_item->port, q_item->pbuf);
	    q_item->timeout = 0;
	}
    }
}

static uint32_t time_of_day = 0;

static void
time_recv(void *arg, struct udp_pcb *upcb, struct pbuf *p,
    struct ip_addr *addr, u16_t port)
{
    printf("Got time packet?!\n");
    memcpy(&time_of_day, p->payload, sizeof(time_of_day));
    printf("Sending blah\n");
    L4_Send((L4_ThreadId_t) (uintptr_t) arg);
}


int
init_transport(struct ip_addr server)
{
    struct pbuf *pbuf;

    udp_cnx = udp_new();
    udp_recv(udp_cnx, time_recv, (void*) sos_my_tid().raw);
    udp_bind(udp_cnx, IP_ADDR_ANY, NFS_LOCAL_PORT);
    udp_connect(udp_cnx, &server, 37 /* Time port */);

    do {
	pbuf = pbuf_alloc(PBUF_TRANSPORT, UDP_SIZE, PBUF_RAM);
	udp_send(udp_cnx, pbuf);
	sos_usleep(100);
	pbuf_free(pbuf);
    } while (time_of_day == 0);

    udp_remove(udp_cnx);

    cur_xid = time_of_day * 10000; /* Generate a randomish xid */

    udp_cnx = udp_new();
    udp_recv(udp_cnx, my_recv, NULL);
    udp_connect(udp_cnx, &server, 0);
    udp_bind(udp_cnx, IP_ADDR_ANY, NFS_LOCAL_PORT);

    return 0;
}

int
rpc_send(struct pbuf *pbuf, int port, 
     void (*func)(void *, uintptr_t, struct pbuf *), 
     void *callback, uintptr_t arg)
{
    pbuf->len =
	pbuf->tot_len = (char *) pbuf->arg[0] - (char *) pbuf->payload;

    /* Add to a queue */
    add_to_queue(pbuf, port, func, callback, arg);

    udp_send_to(udp_cnx, port, pbuf);
    return 0;
}

/********************************************************
*  General functions
*********************************************************/
static xid_t
extract_xid(char *data)
{
    xid_t xid;

    /* extract the xid */
    memcpy(&xid, data, sizeof(xid_t));

    return xid;
}
