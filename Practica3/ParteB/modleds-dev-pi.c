#include <linux/kernel.h>
#include <linux/module.h>
#include <asm-generic/errno.h>
#include <linux/gpio.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/uaccess.h>  /* for copy_from_user */

MODULE_DESCRIPTION("ModledsPi_dev Kernel Module - FDI-UCM");
MODULE_AUTHOR("Juan Carlos Saez");
MODULE_LICENSE("GPL");

int init_module(void);
void cleanup_module(void);
static int device_open(struct inode *, struct file *);
static int device_release(struct inode *, struct file *);
static ssize_t device_read(struct file *, char *, size_t, loff_t *);
static ssize_t device_write(struct file *, const char *, size_t, loff_t *);

#define ALL_LEDS_OFF 0
#define NR_GPIO_LEDS  3
#define DEVICE_NAME "leds"
#define CLASS_NAME "cool"
#define BUFFER_LENGTH 6

static dev_t start;
static struct cdev* chardev = NULL;
static struct class* class = NULL;
static struct device* device = NULL;

static int Device_Open = 0; // Is device open?

/* Actual GPIOs used for controlling LEDs */
const int led_gpio[NR_GPIO_LEDS] = {25, 27, 4};

/* Array to hold gpio descriptors */
struct gpio_desc* gpio_descriptors[NR_GPIO_LEDS];

static struct file_operations fops = {
    .read = device_read,
    .write = device_write,
    .open = device_open,
    .release = device_release
};

//Pone los permisos para el fichero del dispositivo
static char *cool_devnode(struct device *dev, umode_t *mode)
{
    if (!mode)
        return NULL;
    if (MAJOR(dev->devt) == MAJOR(start))
        *mode = 0666;
    return NULL;
}

/* Set led state to that specified by mask */
static inline int set_pi_leds(unsigned int mask) {
	int i;
	for (i = 0; i < NR_GPIO_LEDS; i++)
	gpiod_set_value(gpio_descriptors[i], (mask >> i) & 0x1 );
	return 0;
}

int init_module(void)
{
	int i, j;
	int major;      /* Major number assigned to our device driver */
	int minor;      /* Minor number assigned to the associated character device */
	int ret;
	int err = 0;
	char gpio_str[10];

	for (i = 0; i < NR_GPIO_LEDS; i++) {
	/* Build string ID */
	sprintf(gpio_str, "led_%d", i);
	/* Request GPIO */
		if ((err = gpio_request(led_gpio[i], gpio_str))) {
			pr_err("Failed GPIO[%d] %d request\n", i, led_gpio[i]);
			goto err_handle;
		}

	/* Transforming into descriptor */
		if (!(gpio_descriptors[i] = gpio_to_desc(led_gpio[i]))) {
			pr_err("GPIO[%d] %d is not valid\n", i, led_gpio[i]);
			err = -EINVAL;
			goto err_handle;
		}

		gpiod_direction_output(gpio_descriptors[i], 0);
	}
	if ((ret = alloc_chrdev_region (&start, 0, 1, DEVICE_NAME))) {
        	printk(KERN_INFO "Can't allocate chrdev_region()");
        	return ret;
    	}

	    /* Create associated cdev */
	if ((chardev = cdev_alloc()) == NULL) {
		printk(KERN_INFO "cdev_alloc() failed ");
		ret = -ENOMEM;
		goto error_alloc;
	}

	cdev_init(chardev, &fops);

	if ((ret = cdev_add(chardev, start, 1))) {
		printk(KERN_INFO "cdev_add() failed ");
		goto error_add;
	}

    	/* Create custom class */
    	class = class_create(THIS_MODULE, CLASS_NAME);

	if (IS_ERR(class)) {
		pr_err("class_create() failed \n");
		ret = PTR_ERR(class);
		goto error_class;
	}

	/* Establish function that will take care of setting up permissions for device file */
	class->devnode = cool_devnode;

	/* Creating device */
	device = device_create(class, NULL, start, NULL, DEVICE_NAME);

	if (IS_ERR(device)) {
		pr_err("Device_create failed\n");
		ret = PTR_ERR(device);
		goto error_device;
	}
    	major = MAJOR(start);
    	minor = MINOR(start);

    	printk(KERN_INFO "I was assigned major number %d. To talk to\n", major);
    	printk(KERN_INFO "the driver try to cat and echo to /dev/%s.\n", DEVICE_NAME);
    	printk(KERN_INFO "Remove the module when done.\n");

	return 0;

	err_handle:
		for (j = 0; j < i; j++)
		gpiod_put(gpio_descriptors[j]);
		return err;
	error_device:
    		class_destroy(class);
	error_class:
    	/* Destroy chardev */
    		if (chardev) {
        		cdev_del(chardev);
        		chardev = NULL;
    		}
	error_add:
    	/* Destroy partially initialized chardev */
    	if (chardev)
        	kobject_put(&chardev->kobj);
	error_alloc:
    		unregister_chrdev_region(start, 1);

    		return ret;
}

void cleanup_module(void) {
	int i = 0;

    	if (device)
        	device_destroy(class, device->devt);

    	if (class)
        	class_destroy(class);

    	/* Destroy chardev */
    	if (chardev)
        	cdev_del(chardev);

    	/*
     	* Release major minor pair
     	*/
    	unregister_chrdev_region(start, 1);

	set_pi_leds(ALL_LEDS_OFF);

	for (i = 0; i < NR_GPIO_LEDS; i++)
	gpiod_put(gpio_descriptors[i]);
    	/* Destroy the device and the class */
}

/*
 * Called when a process tries to open the device file, like
 * "cat /dev/chardev"
 */
static int device_open(struct inode *inode, struct file *file)
{
    	if (Device_Open)
        	return -EBUSY;

    	Device_Open++;

    	/* Increment the module's reference counter */
    	try_module_get(THIS_MODULE);

    	return 0;
}

static int device_release(struct inode *inode, struct file *file)
{
    Device_Open--;      /* We're now ready for our next caller */

    /*
     * Decrement the usage count, or else once you opened the file, you'll
     * never get get rid of the module.
     */
    module_put(THIS_MODULE);

    return 0;
}

/*
 * Called when a process, which already opened the dev file, attempts to
 * read from it.
 */
static ssize_t device_read(struct file *filp,   /* see include/linux/fs.h   */
                           char *buffer,    /* buffer to fill with data */
                           size_t length,   /* length of the buffer     */
                           loff_t * offset)
{
    	printk(KERN_ALERT "Sorry, this operation isn't supported.\n");
    	return -EPERM;
}

static ssize_t
device_write(struct file *filp, const char *buff, size_t len, loff_t * off)
{
	char * kbuf = kmalloc(BUFFER_LENGTH, GFP_KERNEL);
	int leds;
	if((*off) > 0){
		kfree(kbuf);
		return 0;
	}
	if(len > BUFFER_LENGTH - 1){
		kfree(kbuf);
		return -ENOSPC;
	}
	if(copy_from_user(kbuf, buff, len)){
		kfree(kbuf);
		return -EINVAL;
	}

	kbuf[len] = '\0';

	if(sscanf(kbuf, "%i", &leds) != 1){
		kfree(kbuf);
		return -EINVAL;
	}

	if(leds < 0 || leds > 7){
		kfree(kbuf);
		return -EINVAL;
	}

	leds = (((leds & 4) >> 2)|(leds & 2) | ((leds & 1) << 2));
	
	set_pi_leds(leds);

	*off += len;
	kfree(kbuf);

	return len;
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Modleds");
