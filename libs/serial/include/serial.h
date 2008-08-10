extern struct serial *serial_init(void);
extern int serial_send(struct serial *serial, char *data, int len);
extern int serial_register_handler(struct serial *serial, 
			void (*handler) (struct serial *serial, char c));
