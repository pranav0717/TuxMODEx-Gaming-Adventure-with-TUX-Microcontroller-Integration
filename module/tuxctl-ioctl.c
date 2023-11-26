/* tuxctl-ioctl.c
 *
 * Driver (skeleton) for the mp2 tuxcontrollers for ECE391 at UIUC.
 *
 * Mark Murphy 2006
 * Andrew Ofisher 2007
 * Steve Lumetta 12-13 Sep 2009
 * Puskar Naha 2013
 */

#include <asm/current.h>
#include <asm/uaccess.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/file.h>
#include <linux/miscdevice.h>
#include <linux/kdev_t.h>
#include <linux/spinlock.h>
#include <linux/tty.h>
#include "tuxctl-ld.h"
#include "tuxctl-ioctl.h"
#include "mtcp.h"

//my intis
int tux_init(struct tty_struct* tty);
int tux_set_led(struct tty_struct* tty, unsigned long arg);
int tux_set_button(struct tty_struct* tty, unsigned long arg);
//my intis


unsigned char button_condition[1];
unsigned char buttonPacekts[2];
unsigned char state_led[6];
unsigned char ledPacket[6]; /* Buffer that holds the final LED data to send to TUX */
int ack;
#define debug(str, ...) \
	printk(KERN_DEBUG "%s: " str, __FUNCTION__, ## __VA_ARGS__)

/************************ Protocol Implementation *************************/

/* tuxctl_handle_packet()
 * IMPORTANT : Read the header for tuxctl_ldisc_data_callback() in 
 * tuxctl-ld.c. It calls this function, so all warnings there apply 
 * here as well.
 */
void tuxctl_handle_packet (struct tty_struct* tty, unsigned char* packet)
{
    unsigned a, b, c;

    a = packet[0]; /* Avoid printk() sign extending the 8-bit */
    b = packet[1]; /* values when printing them. */
    c = packet[2];

	unsigned char b1;
	unsigned char b2;
	unsigned char fifth_bit;
	unsigned char sixth_bit;


	switch(a) {
		case MTCP_ACK:
			ack = 1;
			break;

		case MTCP_BIOC_EVENT:

		// Extract the lower 4 bits of 'b' and store them in 'b1'
		b1 = b & 0x0F;

		// Shift the value of 'c' left by 4 positions (equivalent to multiplying by 16)
		c = c << 4;

		// Extract the upper 4 bits of the shifted 'c' and store them in 'b2'
		b2 = c & 0xF0;

		// Check the fifth bit of 'b2' using a bitmask (1 << 5)
		fifth_bit = b2 & (1 << 5);

		// Check the sixth bit of 'b2' using a bitmask (1 << 6)
		sixth_bit = b2 & (1 << 6);

		// Clear the fifth and sixth bits of 'b2' using a bitmask ~(1 << 5) | ~(1 << 6)
		b2 &= ~((1 << 5) | (1 << 6));

		// Set the fifth and sixth bits of 'b2' based on the values of 'fifth_bit' and 'sixth_bit'
		b2 |= ((fifth_bit << 1) | (sixth_bit >> 1));

		button_condition[0] = (b1 | b2); // Combine RDLU and CBAS to update button_status[0].
			break;

		case MTCP_RESET:
			tux_init(tty);
			tuxctl_ldisc_put(tty,state_led,6);
			break;
	}
	
    /*printk("packet : %x %x %x\n", a, b, c); */
}

/******** IMPORTANT NOTE: READ THIS BEFORE IMPLEMENTING THE IOCTLS ************
 *                                                                            *
 * The ioctls should not spend any time waiting for responses to the commands *
 * they send to the controller. The data is sent over the serial line at      *
 * 9600 BAUD. At this rate, a byte takes approximately 1 millisecond to       *
 * transmit; this means that there will be about 9 milliseconds between       *
 * the time you request that the low-level serial driver send the             *
 * 6-byte SET_LEDS packet and the time the 3-byte ACK packet finishes         *
 * arriving. This is far too long a time for a system call to take. The       *
 * ioctls should return immediately with success if their parameters are      *
 * valid.                                                                     *
 *                                                                            *
 ******************************************************************************/
int 
tuxctl_ioctl (struct tty_struct* tty, struct file* file, 
	      unsigned cmd, unsigned long arg)
{
    switch (cmd) {
	case TUX_INIT:
		return tux_init(tty);
	case TUX_BUTTONS:
		return tux_set_button(tty, arg);
	case TUX_SET_LED:
		return tux_set_led(tty, arg);
	case TUX_LED_ACK:
	case TUX_LED_REQUEST:
	case TUX_READ_LED:
	default:
	    return -EINVAL;
    }
}

/* 
 * tux_init
 *   DESCRIPTION: Initializes a Tux controller by setting the LED and button condition.
 *   INPUTS: tty - pointer to a tty_struct (not used in this function)
 *   OUTPUTS: none
 *   RETURN VALUE: 0 on success
 *   SIDE EFFECTS: Sets LED and button condition, and acknowledges initialization.
 */
int tux_init(struct tty_struct* tty) {
    uint8_t buffer[2];

    // Set the LED to "user mode" and enable the "BIOC" feature
    buffer[0] = MTCP_LED_USR;
    buffer[1] = MTCP_BIOC_ON;
    tuxctl_ldisc_put(tty, buffer, 2);

    // Initialize the button condition to all buttons released (0xff)
    button_condition[0] = 0xff;

    // Acknowledge initialization by setting 'ack' to 1
    ack = 1;

    // Return 0 to indicate success
    return 0;
}

/* 
 * set_led
 *   DESCRIPTION: Sets the LEDs and dot matrix display on a Tux controller based on the provided arguments.
 *   INPUTS: led_bit - an integer (0-15) representing which LEDs to turn on (0x0F for all LEDs off)
 *           dot_bit - an integer (0-15) representing which dot matrix segments to turn on (0x0F for all off)
 *           tty - pointer to a tty_struct (not used in this function)
 *   OUTPUTS: none
 *   RETURN VALUE: 0 on success
 *   SIDE EFFECTS: Updates the LED and dot matrix display on the Tux controller.
 */

int tux_set_led(struct tty_struct *tty, unsigned long arg) {
    unsigned char ledBit;             /* Determines which LED to turn on */
    unsigned char dotBit;             /* Determines which dot to turn on */
    int i;                            /* Variable used for looping */

    if (ack == 0) {
        return 0;
    }
	ack = 0;

    unsigned char ledTable[16] = {
        0xE7, 0x06, 0xCB, 0x8F, 0x2E, 0xAD, 0xED, 0x86,
        0xEF, 0xAE, 0xEE, 0x6D, 0xE1, 0x4F, 0xE9, 0xE8
    };

    unsigned char displayValue[4];     /* Buffer that holds display data */

    /* Initialize the display with zero */
    for (i = 0; i < 6; i++) {
        ledPacket[i] = 0x00;       /* Empty */
    }

    /* Mask input arg and shift to get ledBit and dotBit */
    ledBit = (arg >> 16) & 0x0F;       /* Get low 4 bits of the third byte */
    dotBit = (arg >> 24) & 0x0F;

	ledPacket[0] = MTCP_LED_SET;
	ledPacket[1] = 0x0f;

    /* Save hexadecimal values whose LED is set to ON */
    for (i = 0; i < 4; i++) {
        displayValue[i] = (arg >> (i * 4)) & 0x0F; 
        if (((ledBit >> i) & 0x01) == 0x01) {
            ledPacket[i + 2] = ledTable[displayValue[i]];
        }
        if (((dotBit >> i) & 0x01) == 0x01) {
            /* Set dots to ON accordingly */
            ledPacket[i + 2] |= 0x10;  /* DOT_MASK */
        }
    }

    /* Save the current state to a buffer for RESET */
    for (i = 0; i < 6; i++) {
        state_led[i] = ledPacket[i];
    }

    tuxctl_ldisc_put(tty, ledPacket,6); /* Always send all to erase previous data, send the packet to TUX for LED */\
    return 0;
}


/* 
 * tux_set_button
 *   DESCRIPTION: Sets the button condition for a Tux controller and copies it to the user space.
 *   INPUTS: tty - pointer to a tty_struct (not used in this function)
 *           arg - an unsigned long value representing the button condition to set
 *   OUTPUTS: none
 *   RETURN VALUE: 0 on success, -EINVAL if 'arg' is zero
 *   SIDE EFFECTS: Updates the button condition and copies it to user space.
 */
int tux_set_button(struct tty_struct* tty, unsigned long arg) {
    int ret;

    // Check if 'arg' is zero; return -EINVAL if it is
    if (!arg) {
        return -EINVAL;
    }

    // Copy the 'button_condition' to user space
    ret = copy_to_user((unsigned long*)arg, (&button_condition), 1);

    // Return 0 to indicate success
    return 0;
}
