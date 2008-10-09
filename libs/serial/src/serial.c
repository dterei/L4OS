#include <assert.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include <lwip/netif.h>
#include <lwip/pbuf.h>
#include <lwip/udp.h>

#include "serial/serial.h"
#include "constants.h"
#include "libsos.h"

#define verbose 1

struct serial {
	void (*fHandler) (struct serial *serial, char c);
	struct udp_pcb *fUpcb;
};

static void 
serial_recv_handler(void *vSerial, struct udp_pcb *unused0, 
		struct pbuf *p, struct ip_addr *unused1, u16_t unused2)
{
	struct serial *serial = (struct serial *) vSerial;
	int len = p->len; assert(len);

	if (serial->fHandler) {
		char *cp = p->payload;

		do {
			(*serial->fHandler)(serial, *cp++);
		} while(--len);
	}

	pbuf_free(p);
}

struct serial *
serial_init(void)
{
	static struct serial serial; 
	serial.fUpcb = udp_new();
	if (!serial.fUpcb)
		assert("udp_new");

	u16_t port = SERIAL_PORT;
	if (udp_bind(serial.fUpcb, &netif_default->ip_addr, port))
		assert(!"udp_bind");
	if (udp_connect(serial.fUpcb, &netif_default->gw, port))
		assert(!"udp_connect");
	udp_recv(serial.fUpcb, &serial_recv_handler, &serial);

	return &serial;
}

int
serial_send(struct serial *serial, char *data, int len)
{
	int slen = len;
	struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM);
	assert(p);
	memcpy(p->payload, data, len);
	if (slen&1) ((char *) p->payload)[slen++] = '\0';
	p->tot_len = p->len = slen;
	int r = udp_send(serial->fUpcb, p);
	pbuf_free(p);
	if (r != ERR_OK) {
		dprintf(0, "!!! udp_send failed (len %d) (p %p) (r %d)\n", len, p, r);	
		assert(!"udp_send");
	}

	return len;
}

int
serial_register_handler(struct serial *serial,
		void (*handler)(struct serial *serial, char c))
{
	serial->fHandler = handler;
	return 0;
}

