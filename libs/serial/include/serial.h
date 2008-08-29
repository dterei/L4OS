#ifndef _SERIAL_H
#define _SERIAL_H

extern struct serial *serial_init(void);
extern int serial_send(struct serial *serial, char *data, int len);
extern int serial_register_handler(struct serial *serial, 
			void (*handler) (struct serial *serial, char c));

#endif
<<<<<<< HEAD:libs/serial/include/serial.h

=======
>>>>>>> c4fe45bf3cd1ac066741fc2f22400c4f69847062:libs/serial/include/serial.h
