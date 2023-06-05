#include <linux/module.h>
#include <asm-generic/errno.h>
#include <linux/init.h>
#include <linux/tty.h> /* For fg_console */
#include <linux/kd.h>  /* For KDSETLED */
#include <linux/vt_kern.h>
#include <linux/pwm.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/gpio.h>
#include <linux/miscdevice.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/timer.h>

MODULE_DESCRIPTION("Test-buzzer Kernel Module - FDI-UCM");
MODULE_AUTHOR("Juan Carlos Saez");
MODULE_LICENSE("GPL");

/*
 *  Prototypes
 */
int init_module(void);
void cleanup_module(void);
static ssize_t device_read(struct file *, char *, size_t, loff_t *);
static ssize_t device_write(struct file *, const char *, size_t, loff_t *);

/* Frequency of selected notes in centihertz */
#define C4 26163
#define D4 29366
#define E4 32963
#define F4 34923
#define G4 39200
#define C5 52325

#define MANUAL_DEBOUNCE

#define BUFFER_LENGTH 	PAGE_SIZE
#define DEVICE_NAME "buzzer"
#define PWM_DEVICE_NAME "pwmchip0"
#define GPIO_BUTTON 22
struct gpio_desc* desc_button = NULL;
static int gpio_button_irqn = -1;

struct pwm_device *pwm_device = NULL;
struct pwm_state pwm_state;

/* Work descriptor */
struct work_struct my_work;
struct timer_list my_timer;

static spinlock_t sp; /* Cerrojo para proteger actualización/consulta de variables buzzer_state y buzzer_request */
unsigned long flags;
struct music_step *melody;

static struct music_step* next_note=NULL; 		/* Puntero a la siguiente nota de la melodía 
											actual  (solo alterado por tarea diferida) */

typedef enum {
    BUZZER_STOPPED, /* Buzzer no reproduce nada (la melodía terminó o no ha comenzado) */
    BUZZER_PAUSED,	/* Reproducción pausada por el usuario */
    BUZZER_PLAYING	/* Buzzer reproduce actualmente la melodía */
} buzzer_state_t;

static buzzer_state_t buzzer_state=BUZZER_STOPPED; /* Estado actual de la reproducción */

typedef enum {
    REQUEST_START,		/* Usuario pulsó SW1 durante estado BUZZER_STOPPED */
    REQUEST_RESUME,		/* Usuario pulsó SW1 durante estado BUZZER_PAUSED */
    REQUEST_PAUSE,		/* Usuario pulsó SW1 durante estado BUZZER_PLAYING */
    REQUEST_CONFIG,		/* Usuario está configurando actualmente una nueva melodía vía /dev/buzzer  */
    REQUEST_NONE			/* Indicador de petición ya gestionada (a establecer por tarea diferida) */
} buzzer_request_t;

static buzzer_request_t buzzer_request=REQUEST_NONE;

static int beat = 120; /* 120 quarter notes per minute */

static struct file_operations fops = {
    .read = device_read,
    .write = device_write
};


static struct miscdevice misc_buzzer = {
    .minor = MISC_DYNAMIC_MINOR,    /* kernel dynamically assigns a free minor# */
    .name = DEVICE_NAME, /* when misc_register() is invoked, the kernel
                        * will auto-create device file as /dev/chardev ;
                        * also populated within /sys/class/misc/ and /sys/devices/virtual/misc/ */
    .mode = 0666,     /* ... dev node perms set as specified here */
    .fops = &fops,    /* connect to this driver's 'functionality' */
};

/* Structure to represent a note or rest in a melodic line  */
struct music_step
{
	unsigned int freq : 24; /* Frequency in centihertz */
	unsigned int len : 8;	/* Duration of the note */
};

/* Transform frequency in centiHZ into period in nanoseconds */
static inline unsigned int freq_to_period_ns(unsigned int frequency)
{
	if (frequency == 0)
		return 0;
	else
		return DIV_ROUND_CLOSEST_ULL(100000000000UL, frequency);
}

/* Check if the current step is and end marker */
static inline int is_end_marker(struct music_step *step)
{
	return (step->freq == 0 && step->len == 0);
}

/**
 *  Transform note length into ms,
 * taking the beat of a quarter note as reference
 */
static inline int calculate_delay_ms(unsigned int note_len, unsigned int qnote_ref)
{
	unsigned char duration = (note_len & 0x7f);
	unsigned char triplet = (note_len & 0x80);
	unsigned char i = 0;
	unsigned char current_duration;
	int total = 0;

	/* Calculate the total duration of the note
	 * as the summation of the figures that make
	 * up this note (bits 0-6)
	 */
	while (duration) {
		current_duration = (duration) & (1 << i);

		if (current_duration) {
			/* Scale note accordingly */
			if (triplet)
				current_duration = (current_duration * 3) / 2;
			/*
			 * 24000/qnote_ref denote number of ms associated
			 * with a whole note (redonda)
			 */
			total += (240000) / (qnote_ref * current_duration);
			/* Clear bit */
			duration &= ~(1 << i);
		}
		i++;
	}
	return total;
}


/* Work's handler function */
static void my_wq_function(struct work_struct *work)
{
	spin_lock_irqsave(&sp, flags);

	if(buzzer_request == REQUEST_START){
		buzzer_state = BUZZER_PLAYING;
		buzzer_request = REQUEST_NONE;
		next_note = &melody[0];
		spin_unlock_irqrestore(&sp, flags);

		//pwm_init_state(pwm_device, &pwm_state);
		/* Obtain period from frequency */
		pwm_state.period = freq_to_period_ns(next_note->freq);
		/**
		* Disable temporarily to allow repeating the same consecutive
		* notes in the melodic line
		 **/
		pwm_disable(pwm_device);
		/* If period==0, its a rest (silent note) */
		if (pwm_state.period > 0) {
			/* Set duty cycle to 70 to maintain the same timbre */
			pwm_set_relative_duty_cycle(&pwm_state, 70, 100);
			pwm_state.enabled = true;
			/* Apply state */
			pwm_apply_state(pwm_device, &pwm_state);
		} else {
			/* Disable for rest */
			pwm_disable(pwm_device);
		}
		/* Wait for duration of the note or reset */
		mod_timer(&my_timer, jiffies + msecs_to_jiffies(calculate_delay_ms(next_note->len, beat)));

		next_note++;
	}

	else if(buzzer_request == REQUEST_RESUME || (buzzer_request == REQUEST_NONE && buzzer_state == BUZZER_PLAYING)){
		buzzer_state = BUZZER_PLAYING;
		buzzer_request = REQUEST_NONE;
		spin_unlock_irqrestore(&sp, flags);

		if(!is_end_marker(next_note)){

			/* Obtain period from frequency */
			pwm_state.period = freq_to_period_ns(next_note->freq);
			/**
			* Disable temporarily to allow repeating the same consecutive
			* notes in the melodic line
			 **/
			pwm_disable(pwm_device);
			/* If period==0, its a rest (silent note) */
			if (pwm_state.period > 0) {
				/* Set duty cycle to 70 to maintain the same timbre */
				pwm_set_relative_duty_cycle(&pwm_state, 70, 100);
				pwm_state.enabled = true;
				/* Apply state */
				pwm_apply_state(pwm_device, &pwm_state);
			} else {
				/* Disable for rest */
				pwm_disable(pwm_device);
			}
				/* Wait for duration of the note or reset */
				mod_timer(&my_timer, jiffies + msecs_to_jiffies(calculate_delay_ms(next_note->len, beat)));
		}
		else{
			pwm_disable(pwm_device);
			buzzer_state = BUZZER_STOPPED;
		}
		next_note++;
	}
	else if(buzzer_request == REQUEST_PAUSE){
		buzzer_state = BUZZER_PAUSED;
		buzzer_request = REQUEST_NONE;
		spin_unlock_irqrestore(&sp, flags);
		pwm_disable(pwm_device);
	}
	else if(buzzer_request == REQUEST_CONFIG){
		buzzer_state = BUZZER_STOPPED;
		buzzer_request = REQUEST_NONE;
		spin_unlock_irqrestore(&sp, flags);
		pwm_disable(pwm_device);
	}

}

/* Function invoked when timer expires (fires) */
static void fire_timer(struct timer_list *timer)
{
	spin_lock_irqsave(&sp, flags);
	if(buzzer_state == BUZZER_PLAYING)
		schedule_work(&my_work);

	spin_unlock_irqrestore(&sp, flags);

}

/* Interrupt handler for button **/
static irqreturn_t gpio_irq_handler(int irq, void *dev_id)
{
#ifdef MANUAL_DEBOUNCE
  	static unsigned long last_interrupt = 0;
  	unsigned long diff = jiffies - last_interrupt;
  	if (diff < 20)
    	return IRQ_HANDLED;

  	last_interrupt = jiffies;
#endif
	spin_lock_irqsave(&sp, flags);
	if(buzzer_state == BUZZER_STOPPED){
		buzzer_request = REQUEST_START;
	}
	else if(buzzer_state == BUZZER_PAUSED){
		buzzer_request = REQUEST_RESUME;
	}
	else if(buzzer_state == BUZZER_PLAYING){
		buzzer_request = REQUEST_PAUSE;
	}

	spin_unlock_irqrestore(&sp, flags);
	/* Enqueue work */
	schedule_work(&my_work);
	
	
  return IRQ_HANDLED;
}

/* Called when a user program invokes the write() system call on the device */
static ssize_t device_write(struct file *file, const char *user_buffer, size_t len, loff_t *off){
	
	int avalible_space = BUFFER_LENGTH - 1;
	unsigned int  freq, dur, n;
	struct music_step* i;
	char *kbuf = (char*) kmalloc(sizeof(char)*BUFFER_LENGTH + 5, GFP_KERNEL);
	char *song = (char*) kmalloc(sizeof(char)*PAGE_SIZE, GFP_KERNEL);
	char *sepMsg = (char*) kmalloc(sizeof(char)* 33, GFP_KERNEL);
	if((*off) > 0){
		kfree(kbuf);
		spin_unlock_irqrestore(&sp, flags);
		return 0;
	}
	if(len > avalible_space) {
		printk(KERN_INFO "buzzer: not enough space\n");
		kfree(kbuf);
		kfree(song);
		kfree(sepMsg);
		spin_unlock_irqrestore(&sp, flags);
		return -ENOSPC;
	}
	if(copy_from_user(kbuf, user_buffer, len)){
		kfree(kbuf);
		kfree(song);
		kfree(sepMsg);
		spin_unlock_irqrestore(&sp, flags);
		return -EINVAL;
	}
	kbuf[len] = '\0';
	spin_lock_irqsave(&sp, flags);
	if(sscanf(kbuf, "music %s", song) == 1){
		if(buzzer_state == BUZZER_PLAYING){
			kfree(kbuf);
			kfree(song);
			kfree(sepMsg);
			spin_unlock_irqrestore(&sp, flags);
			return -EBUSY;
		}
		if(strcmp(kbuf, "\n\0") != 0){
		
			memset(melody,0,PAGE_SIZE);
			i = &melody[0];
			while((sepMsg = strsep(&song, ",")) != NULL){
				/* Fill up the message accordingly */
				if(sscanf(sepMsg, "%i:%x", &freq, &dur) == 2){
					i->freq = freq;
					i->len = dur;
					i++;
				}
				else{
					kfree(sepMsg);
					kfree(song);
					kfree(kbuf);
					printk(KERN_ALERT "ERROR: No valid value\n");
					spin_unlock_irqrestore(&sp, flags);
					return -EINVAL;
				}
			}
			buzzer_request = REQUEST_CONFIG;
			schedule_work(&my_work);
		}
	}
	else if(sscanf(kbuf, "beat %i", &n) == 1){
		beat = n;
	}
	else{
		kfree(kbuf);
		kfree(song);
		kfree(sepMsg);
		spin_unlock_irqrestore(&sp, flags);
		return -EINVAL;
	}
	spin_unlock_irqrestore(&sp, flags);
	*off += len;
	kfree(kbuf);
	kfree(song);
	kfree(sepMsg);

	return len;
}

static ssize_t device_read(struct file *file, char *user_buffer, size_t len, loff_t *off){
	int nr_bytes = 0;
	char *kbuf = (char*) kmalloc(sizeof(char)*BUFFER_LENGTH, GFP_KERNEL);
	if((*off) > 0){
		kfree(kbuf);
		return 0;
	}
	spin_lock_irqsave(&sp, flags);
	nr_bytes = sprintf(kbuf, "beat=%i\n", beat);

	if(copy_to_user(user_buffer, kbuf, nr_bytes)){
		kfree(kbuf);
		return -EINVAL;
	}
	spin_unlock_irqrestore(&sp, flags);
	(*off) += len;
	kfree(kbuf);


	return nr_bytes;	
}

int init_module(void)
{
    	int ret, err;
	unsigned char gpio_out_ok = 0;
	struct device *device;

	  /* Requesting Button's GPIO */
  	if ((err = gpio_request(GPIO_BUTTON, "button"))) {
    		pr_err("ERROR: GPIO %d request\n", GPIO_BUTTON);
    		goto err_handle;
  	}

  	/* Configure Button */
	if (!(desc_button = gpio_to_desc(GPIO_BUTTON))) {
		pr_err("GPIO %d is not valid\n", GPIO_BUTTON);
		err = -EINVAL;
		goto err_handle;
	}

	gpio_out_ok = 1;

	//configure the BUTTON GPIO as input
	gpiod_direction_input(desc_button);

	/*
	** The lines below are commented because gpiod_set_debounce is not supported
	** in the Raspberry pi. Debounce is handled manually in this driver.
	*/
	#ifndef MANUAL_DEBOUNCE
	//Debounce the button with a delay of 200ms
	if (gpiod_set_debounce(desc_button, 200) < 0) {
		pr_err("ERROR: gpio_set_debounce - %d\n", GPIO_BUTTON);
		goto err_handle;
	}
	#endif

	//Get the IRQ number for our GPIO
	gpio_button_irqn = gpiod_to_irq(desc_button);
	pr_info("IRQ Number = %d\n", gpio_button_irqn);

	if (request_irq(gpio_button_irqn,             //IRQ number
		gpio_irq_handler,   //IRQ handler
			IRQF_TRIGGER_RISING,        //Handler will be called in raising edge
			"button_leds",               //used to identify the device name using this IRQ
			NULL)) {                    //device id for shared IRQ
	    	pr_err("my_device: cannot register IRQ ");
	    	goto err_handle;
	  }
	/* Request utilization of PWM0 device */
	pwm_device = pwm_request(0, PWM_DEVICE_NAME);

	if (IS_ERR(pwm_device))
		return PTR_ERR(pwm_device);

	ret = misc_register(&misc_buzzer);

	if (ret) {
		pr_err("Couldn't register misc device\n");
		return ret;
	}

	device = misc_buzzer.this_device;
	timer_setup(&my_timer, fire_timer, 0);

	melody = vmalloc(PAGE_SIZE);
	memset(melody,0,PAGE_SIZE);

	melody[0].freq = C4; melody[0].len = 4;melody[1].freq = E4; melody[1].len = 4;melody[2].freq = G4; melody[2].len = 4;
	melody[3].freq = C5; melody[3].len = 4;melody[4].freq = 0; melody[4].len = 2;melody[5].freq = C5; melody[5].len = 4;
	melody[6].freq = G4; melody[6].len = 4;melody[7].freq = E4; melody[7].len = 4;melody[8].freq = C4; melody[8].len = 4;
	melody[9].freq = 0; melody[9].len = 0;
	/* Initialize work structure (with function) */
	INIT_WORK(&my_work, my_wq_function);

	pwm_init_state(pwm_device, &pwm_state);

	return 0;

	err_handle:
  	if (gpio_out_ok)
    	gpiod_put(desc_button);

  	return err;
}

void cleanup_module(void)
{
	misc_deregister(&misc_buzzer);
	/* Wait until defferred work has finished */
	flush_work(&my_work);
	pwm_disable(pwm_device);

  	free_irq(gpio_button_irqn, NULL);
	gpiod_put(desc_button);
	vfree(melody);

	/* Release PWM device */
	pwm_free(pwm_device);
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PWM test");
