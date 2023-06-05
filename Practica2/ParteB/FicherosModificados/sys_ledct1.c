#include <linux/syscalls.h> /* For SYSCALL_DEFINEi() */
#include <linux/kernel.h>
#include <linux/kd.h>       /* For KDSETLED */
#include <linux/vt_kern.h>
#include <linux/tty.h>      /* For fg_console */

struct tty_driver* kbd_driver= NULL;

/* Set led state to that specified by mask */
static inline int set_leds(struct tty_driver* handler, unsigned int mask){
    return (handler->ops->ioctl) (vc_cons[fg_console].d->port.tty, KDSETLED,mask);
}

SYSCALL_DEFINE1(ledct1, unsigned int ,leds) 
{
	/* To be completed ... */
	unsigned int mask = 0x0;
	kbd_driver= vc_cons[fg_console].d->port.tty->driver;
	if(leds < 0 || leds > 7)
		return -EINVAL;
	switch(leds){
		case 2:
			mask |= 0x4;
		break;
		case 4:
			mask |= 0x2;
		break;
		case 3:
			mask |= 0x5;
		break;
		case 5:
			mask |= 0x3;
		break;
		default:
			mask |= leds;
		break;
	}
	return set_leds(kbd_driver, mask);
}
