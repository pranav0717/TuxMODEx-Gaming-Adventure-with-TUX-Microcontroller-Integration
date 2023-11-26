// All necessary declarations for the Tux Controller driver must be in this file

#ifndef TUXCTL_H
#define TUXCTL_H


#define TUX_SET_LED _IOR('E', 0x10, unsigned long)
#define TUX_READ_LED _IOW('E', 0x11, unsigned long*)
#define TUX_BUTTONS _IOW('E', 0x12, unsigned long*)
#define TUX_INIT _IO('E', 0x13)
#define TUX_LED_REQUEST _IO('E', 0x14)
#define TUX_LED_ACK _IO('E', 0x15)


// //my intis TOOK EM OUT FOR TTY WARNING!!
// int tux_init(struct tty_struct* tty);
// int tux_set_led(struct tty_struct* tty, unsigned long arg);
// int tux_set_button(struct tty_struct* tty, unsigned long arg);
// //my intis TOOK EM OUT FOR TTY WARNING!!

#endif

