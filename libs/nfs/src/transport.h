/* transport.h */

#ifndef __TRANSPORT_H
#define __TRANSPORT_H

#include <lwip/api.h>

/* these all modify RPC buffers */
void clearbuf(struct pbuf* pbuf );
struct pbuf * initbuf(int prognum, int vernum, int procnum);
void addtobuf(struct pbuf *pbuf, char* data, int len);
void getfrombuf(struct pbuf *pbuf, char* data, int len);
void getstring(struct pbuf *pbuf, char* data, int len);
int  getdata(struct pbuf *pbuf, char* data, int len, int null);
void * getpointfrombuf(struct pbuf *pbuf, int len);
void addstring(struct pbuf *pbuf, char* data);
void adddata(struct pbuf *pbuf, char* data, int size);
void skipstring(struct pbuf *pbuf);

/* do we need this in transport?? */
void change_endian( struct pbuf * pbuf );

xid_t set_rand_xid( int rand );

struct pbuf * rpc_call(struct pbuf* buf, int port);
int
rpc_send(struct pbuf *pbuf, int port, 
	 void (*func)(void *, uintptr_t, struct pbuf *), 
	 void *callback, uintptr_t arg);
void resetbuf(struct pbuf * pbuf);

int init_transport(struct ip_addr server);

struct pbuf * initbuf_xid(xid_t txid, int prognum, 
			  int vernum, int procnum);

#endif /* __TRANSPORT_H */
