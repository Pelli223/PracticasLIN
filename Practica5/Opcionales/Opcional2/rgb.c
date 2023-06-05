#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <asm-generic/errno.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/spinlock.h>
#include <linux/pwm.h>

#define PWM_DEVICE_NAME "pwmchip1"

#define NR_GPIO_RGB  2

const int rgb_gpio[NR_GPIO_RGB] = {26, 21};

static unsigned int mask = 0;

/* Array to hold gpio descriptors */
struct gpio_desc* gpio_descriptors[NR_GPIO_RGB];
struct timer_list my_timer;

struct pwm_device *pwm_device = NULL;
struct pwm_state pwm_state;

struct timer_list my_timer; /* Structure that describes the kernel timer */
/* Work descriptor */
struct work_struct my_work;

/* Set led state to that specified by mask */
static inline int set_pi_rgb(unsigned int mask) {
  int i;
  for (i = 0; i < NR_GPIO_RGB; i++)
    gpiod_set_value(gpio_descriptors[i], (mask >> i) & 0x1 );
  return 0;
}

/* Function invoked when timer expires (fires) */
static void fire_timer(struct timer_list *timer)
{
	mask ++;
	if(mask > 3)
		mask = 0;

    	set_pi_rgb(mask);
    
	schedule_work(&my_work);
}

/* Work's handler function */
static void my_wq_function(struct work_struct *work)
{
	pwm_state.period = 100;
	pwm_disable(pwm_device);
	pwm_set_relative_duty_cycle(&pwm_state, 30 + (10*mask), 100);
	pwm_state.enabled = true;
	/* Apply state */
	pwm_apply_state(pwm_device, &pwm_state);
	my_timer.expires = jiffies + HZ; /* Activate it one second from now */
	add_timer(&my_timer);
}


static int __init rgb_init(void)
{
  int i, j;
  int err = 0;
  char gpio_str[10];



  for (i = 0; i < NR_GPIO_RGB; i++) {
    /* Build string ID */
    sprintf(gpio_str, "led_%d", i);
    //Requesting the GPIO
    if ((err = gpio_request(rgb_gpio[i], gpio_str))) {
      pr_err("Failed GPIO[%d] %d request\n", i, rgb_gpio[i]);
      goto err_handle;
    }

    /* Transforming into descriptor **/
    if (!(gpio_descriptors[i] = gpio_to_desc(rgb_gpio[i]))) {
      pr_err("GPIO[%d] %d is not valid\n", i, rgb_gpio[i]);
      err = -EINVAL;
      goto err_handle;
    }

    gpiod_direction_output(gpio_descriptors[i], 0);
  }

	/* Request utilization of PWM1 device */
	pwm_device = pwm_request(1, PWM_DEVICE_NAME);

	if (IS_ERR(pwm_device))
		return PTR_ERR(pwm_device);

	pwm_init_state(pwm_device, &pwm_state);
  	timer_setup(&my_timer, fire_timer, 0);
  	/* Initialize work structure (with function) */
	INIT_WORK(&my_work, my_wq_function);
	schedule_work(&my_work);
	
  return 0;
err_handle:
  for (j = 0; j < i; j++)
    gpiod_put(gpio_descriptors[j]);

  return err;
}

static void __exit rgb_exit(void) {
  int i = 0;

  set_pi_rgb(0);

  del_timer_sync(&my_timer);
  flush_work(&my_work);

  for (i = 0; i < NR_GPIO_RGB; i++)
    gpiod_put(gpio_descriptors[i]);

pwm_free(pwm_device);

}

module_init(rgb_init);
module_exit(rgb_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Modleds");
