#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/uaccess.h>  /* for copy_to_user */
#include <linux/cdev.h>
#include <linux/slab.h>
#include<linux/list.h>
#include<linux/spinlock.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <asm-generic/errno.h>

MODULE_DESCRIPTION("ChardevData Kernel Module - FDI-UCM");
MODULE_AUTHOR("Juan Carlos Saez");
MODULE_LICENSE("GPL");

/*
 *  Prototypes
 */
int init_module(void);
void cleanup_module(void);
static int device_open(struct inode *, struct file *);
static int device_release(struct inode *, struct file *);
static ssize_t device_read(struct file *, char *, size_t, loff_t *);
static ssize_t device_write(struct file *, const char *, size_t, loff_t *);

#define SUCCESS 0
#define DEVICE_NAME "modcontrol"   /* Proc name as it appears in /proc/modcontrol  */
#define CLASS_NAME "cool"
#define BUFFER_LENGTH 	30      
#define NUM_LEDS_SWITCHES 3

#define MANUAL_DEBOUNCE

static struct proc_dir_entry *proc_entry;
static struct list_head list;
static int major, minor;

DEFINE_SPINLOCK(sp);


const int led_gpio[NUM_LEDS_SWITCHES] = {25, 27, 4};
const int switch_gpio[NUM_LEDS_SWITCHES] = {26, 21, 22};

static unsigned int used [NUM_LEDS_SWITCHES] = {0, 0, 0};

struct led_switch {
	unsigned char cur_led_state; /* Estado del LED correspondiente */
	struct gpio_desc* button_gpio;
	int gpio_button_irqn;
	struct gpio_desc* led_gpio;
	char *devName;
	unsigned int Device_Open;
    	struct device* device;
    	dev_t major_minor;
	spinlock_t sp;
	unsigned long flags;
	int pos;
};

struct list_item{
	struct led_switch *device;
	struct list_head links;
};

/*
 * Global variables are declared as static, so are global within the file.
 */

static dev_t start;
static struct cdev* chardev = NULL;
static struct class* class = NULL;

static int contDev = 0;

static int device_open(struct inode *, struct file *);
static int device_release(struct inode *, struct file *);
static ssize_t device_read(struct file *, char *, size_t, loff_t *);
static ssize_t device_write(struct file *, const char *, size_t, loff_t *);

static struct file_operations fops = {
    .read = device_read,
    .write = device_write,
    .open = device_open,
    .release = device_release
};


/**
 * Set up permissions for device files created by this driver
 *
 * The permission mode is returned using the mode parameter.
 *
 * The return value (unused here) could be used to indicate the directory
 * name under /dev where to create the device file. If used, it should be
 * a string whose memory must be allocated dynamically.
 **/
static char *cool_devnode(struct device *dev, umode_t *mode)
{
    if (!mode)
        return NULL;
    if (MAJOR(dev->devt) == MAJOR(start))
        *mode = 0666;
    return NULL;
}

static irqreturn_t gpio_irq_handler(int irqn, void *dev_data)
{

	struct led_switch *ddata = (struct led_switch*) dev_data;
	#ifdef MANUAL_DEBOUNCE
  	static unsigned long last_interrupt = 0;
  	unsigned long diff = jiffies - last_interrupt;
  	if (diff < 20)
    		return IRQ_HANDLED;

  	last_interrupt = jiffies;
	#endif

	spin_lock_irqsave(&ddata->sp, ddata->flags);
	ddata->cur_led_state = ddata->cur_led_state == 0 ? 1 : 0;
	gpiod_set_value(ddata->led_gpio, ddata->cur_led_state);
	spin_unlock_irqrestore(&ddata->sp, ddata->flags);

	return IRQ_HANDLED;
	
}

static int modcontrol_open(struct inode * i, struct file * f){
	try_module_get(THIS_MODULE);
	return 0;
}

static int modcontrol_release(struct inode * i, struct file * f){
	module_put(THIS_MODULE);
	return 0;
}

static ssize_t modcontrol_write (struct file *filp, const char __user *buf, size_t len, loff_t *off){
	int n,i, ret;
	dev_t majMin;
	int avalible_space = BUFFER_LENGTH + 7;
	char *kbuf = (char*) kmalloc(sizeof(char)*BUFFER_LENGTH + 8, GFP_KERNEL);
	char *devName = kmalloc(sizeof(char) * BUFFER_LENGTH, GFP_KERNEL);
	struct device *dev;
	struct list_head *curr = NULL;
	struct list_head *aux = NULL;
	struct list_item *item = NULL;
	struct led_switch* ddata = kzalloc(sizeof(struct led_switch), GFP_KERNEL);
	ddata->devName = kmalloc(BUFFER_LENGTH, GFP_KERNEL);

	if((*off) > 0){
		kfree(kbuf);
		return 0;
	}
	if(len > avalible_space) {
		printk(KERN_INFO "modcontrol: not enough space\n");
		kfree(kbuf);
		return -ENOSPC;
	}
	if(copy_from_user(kbuf, buf, len)){
		kfree(kbuf);
		kfree(ddata);
		kfree(ddata->devName);
		kfree(devName);
		return -EINVAL;
	}
	kbuf[len] = '\0';
	spin_lock(&sp);
	if(sscanf(kbuf, "new %s", ddata->devName) == 1){

		if(contDev >= 3){
			kfree(kbuf);
			kfree(ddata);
			kfree(ddata->devName);
			kfree(devName);
			spin_unlock(&sp);
			return -ENOSPC;
		}
		list_for_each_safe(curr, aux, &list){
			item = list_entry(curr, struct list_item, links);
			if(strcmp(item->device->devName, ddata->devName) == 0){
				kfree(kbuf);
				kfree(ddata);
				kfree(ddata->devName);
				kfree(devName);
				spin_unlock(&sp);
				return -EINVAL;
			}
		}
		

		for(i = 0; i < 3; i++)
			if(!used[i])
				break;
	
	majMin = MKDEV(major, (minor + i) );

    	/* Proper initialization */
	used[i] = 1;

    	ddata->cur_led_state = 0;
	ddata->pos = i;
    	/* Transforming into descriptor **/
	if (!(ddata->button_gpio = gpio_to_desc(switch_gpio[i]))) {
		pr_err("GPIO[%d] %d is not valid\n", i, switch_gpio[i]);
		ret = -EINVAL;
		goto err_handle;
	}

	gpiod_direction_input(ddata->button_gpio);
	/*
	** The lines below are commented because gpiod_set_debounce is not supported
	** in the Raspberry pi. Debounce is handled manually in this driver.
	*/
	#ifndef MANUAL_DEBOUNCE
  	//Debounce the button with a delay of 200ms
  	if (gpiod_set_debounce(ddata->button_gpio, 200) < 0) {
    		pr_err("ERROR: gpio_set_debounce - %d\n", switch_gpio[i]);
    		goto err_handle;
  	}
	#endif
    	/* Transforming into descriptor **/
	if (!(ddata->led_gpio = gpio_to_desc(led_gpio[i]))) {
		pr_err("GPIO[%d] %d is not valid\n", i, led_gpio[i]);
		ret = -EINVAL;
		goto err_handle;
	}
	gpiod_direction_output(ddata->led_gpio, 0);
    	ddata->Device_Open = 0;
	ddata->gpio_button_irqn = gpiod_to_irq(ddata->button_gpio);
    	ddata->major_minor = majMin; /* Only one device */
	spin_lock_init(&ddata->sp);

	strcpy(devName, "ledsw_");
	strcat(devName, ddata->devName);

	if (request_irq(ddata->gpio_button_irqn,             //IRQ number
                  gpio_irq_handler,   //IRQ handler
                  IRQF_TRIGGER_RISING,        //Handler will be called in raising edge
                  ddata->devName,               //used to identify the device name using this IRQ
                  ddata)) {                    //device id for shared IRQ
		pr_err("my_device: cannot register IRQ ");
		goto err_handle;
	}

		/* Creating device */
	    	ddata->device = device_create(class, NULL, majMin, ddata, "%s", devName);
		if (IS_ERR(ddata->device)) {
        		pr_err("Device_create failed\n");
        		ret = PTR_ERR(ddata->device);
        		goto err_handle;
    		}

		item = kmalloc(sizeof(struct list_item), GFP_KERNEL);

		item->device = ddata;

		list_add_tail(&item->links, &list);
		contDev++;
		ddata->cur_led_state = 0;
		gpiod_set_value(ddata->led_gpio, ddata->cur_led_state);
	}

	else if(sscanf(kbuf, "delete %s", devName) == 1){
		n = 0;
		list_for_each_safe(curr, aux, &list){
			item = list_entry(curr, struct list_item, links);
			if(strcmp(item->device->devName, devName) == 0){
				list_del(curr);
				free_irq(item->device->gpio_button_irqn, item->device);
				gpiod_put(item->device->button_gpio);
				gpiod_put(item->device->led_gpio);
				dev = class_find_device_by_devt(class, item->device->major_minor);
				used[item->device->pos] = 0;
				device_destroy(class, dev->devt);
				kfree(item->device);
				kfree(item->device->devName);
				kfree(item);
				n = 1;
				contDev--;
			}
		}

		if(!n){
			kfree(kbuf);
			kfree(ddata);
			kfree(ddata->devName);
			kfree(devName);
			spin_unlock(&sp);
			return -ENOENT;
		}

	}

	else{
		kfree(kbuf);
		kfree(ddata);
		kfree(ddata->devName);
		kfree(devName);
		spin_unlock(&sp);
		return -EINVAL;
	}
	spin_unlock(&sp);
	*off += len;
	kfree(kbuf);
	kfree(devName);

	return len;

	err_handle:

		kfree(kbuf);
		kfree(ddata->devName);
		kfree(ddata);
		kfree(devName);
		spin_unlock(&sp);
 		return ret;
}

static ssize_t modcontrol_read(struct file *filp, char __user *buf, size_t len, loff_t *off){
	int nr_bytes = 0;
	char *kbuf = (char*) kmalloc(sizeof(char)*BUFFER_LENGTH * 3, GFP_KERNEL);
	struct list_item *item = NULL;
	struct list_head *curr = NULL;
	memset(kbuf, 0, BUFFER_LENGTH * 3);
	if((*off) > 0){
		kfree(kbuf);
		return 0;
	}
	spin_lock(&sp);
	list_for_each(curr, &list) {
		item = list_entry(curr, struct list_item, links);
		nr_bytes += strlen(item->device->devName) + sizeof(char);;
		if(nr_bytes > BUFFER_LENGTH * 3){
			kfree(kbuf);
			spin_unlock(&sp);
			return -ENOSPC;
		}
		sprintf(&kbuf[strlen(kbuf)], "%s ", item->device->devName);
	}
	kbuf[strlen(kbuf)] = '\n';
	nr_bytes += 1;
	printk("%s", kbuf);
	spin_unlock(&sp);
	if(len < nr_bytes){
		kfree(kbuf);
		return -ENOSPC;
	}
	//nr_bytes += 1;
	if(copy_to_user(buf, kbuf, nr_bytes)){
		kfree(kbuf);
		return -EINVAL;
	}
	(*off) += len;
	kfree(kbuf);
	
	return nr_bytes;
}

static struct proc_ops pops = {
	.proc_open = modcontrol_open,
	.proc_release = modcontrol_release,
	.proc_read = modcontrol_read,
	.proc_write = modcontrol_write,
};

/*
 * This function is called when the module is loaded
 */
int init_modcontrol_module(void)
{
    	int ret, i, j;
    	struct led_switch *ddata = NULL;
	struct list_item *item = NULL;
	char gpio_str[10];
	char *devName = kmalloc(BUFFER_LENGTH, GFP_KERNEL);
	
	INIT_LIST_HEAD(&list);
	proc_entry = proc_create(DEVICE_NAME, 0666, NULL, &pops);
	if(proc_entry == NULL){
		ret = -ENOMEM;
		kfree(&list);
		printk(KERN_INFO "ERROR: Cant create module\n");
		return ret;
	}
	else{
		printk(KERN_INFO "Modulo modcontrol cargado\n");
	}
    	/* Get available (major,minor) range */
    	if ((ret = alloc_chrdev_region (&start, 0, 3, DEVICE_NAME))) {
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

    	if ((ret = cdev_add(chardev, start, 3))) {
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

    	/* Allocate device state structure and zero fill it */
    	if ((ddata = kzalloc(sizeof(struct led_switch), GFP_KERNEL)) == NULL) {
        	ret = -ENOMEM;
        	goto error_alloc_state;
    	}

  	for (i = 0; i < NUM_LEDS_SWITCHES; i++) {
    		/* Build string ID */
    		sprintf(gpio_str, "led_%d", i);
    		//Requesting the GPIO
    		if ((ret = gpio_request(led_gpio[i], gpio_str))) {
      			pr_err("Failed GPIO[%d] %d request\n", i, led_gpio[i]);
      			goto err_handle;
    		}

    		/* Build string ID */
    		sprintf(gpio_str, "switch_%d", i);
    		//Requesting the GPIO
    		if ((ret = gpio_request(switch_gpio[i], gpio_str))) {
      			pr_err("Failed GPIO[%d] %d request\n", i, switch_gpio[i]);
      			goto err_handle;
    		}
	 }	

    	/* Proper initialization */
	used[0] = 1;

    	ddata->cur_led_state = 0;
    	/* Transforming into descriptor **/
	if (!(ddata->button_gpio = gpio_to_desc(switch_gpio[0]))) {
		pr_err("GPIO[%d] %d is not valid\n", 0, switch_gpio[0]);
		ret = -EINVAL;
		goto err_handle;
	}

	gpiod_direction_input(ddata->button_gpio);
	/*
	** The lines below are commented because gpiod_set_debounce is not supported
	** in the Raspberry pi. Debounce is handled manually in this driver.
	*/
	#ifndef MANUAL_DEBOUNCE
  	//Debounce the button with a delay of 200ms
  	if (gpiod_set_debounce(ddata->button_gpio, 200) < 0) {
    		pr_err("ERROR: gpio_set_debounce - %d\n", switch_gpio[0]);
    		goto err_handle;
  	}
	#endif
    	/* Transforming into descriptor **/
	if (!(ddata->led_gpio = gpio_to_desc(led_gpio[0]))) {
		pr_err("GPIO[%d] %d is not valid\n", i, led_gpio[0]);
		ret = -EINVAL;
		goto err_handle;
	}
	major = MAJOR(start);
	minor = MINOR(start);
	gpiod_direction_output(ddata->led_gpio, 0);
	ddata->devName = kmalloc(BUFFER_LENGTH, GFP_KERNEL);
	strcpy(ddata->devName, "def");
    	ddata->Device_Open = 0;
	ddata->gpio_button_irqn = gpiod_to_irq(ddata->button_gpio);
    	ddata->major_minor = start; /* Only one device */
	spin_lock_init(&ddata->sp);
	ddata->pos = 0;

	strcpy(devName, "ledsw_");
	strcat(devName, ddata->devName);

	if (request_irq(ddata->gpio_button_irqn,             //IRQ number
                  gpio_irq_handler,   //IRQ handler
                  IRQF_TRIGGER_RISING,        //Handler will be called in raising edge
                  ddata->devName,               //used to identify the device name using this IRQ
                  ddata)) {                    //device id for shared IRQ
		pr_err("my_device: cannot register IRQ ");
		goto err_handle;
	}

	/* Creating device */
    	ddata->device = device_create(class, NULL, ddata->major_minor, ddata, "%s", devName);
	if (IS_ERR(ddata->device)) {
        	pr_err("Device_create failed\n");
        	ret = PTR_ERR(ddata->device);
        	goto err_handle;
    	}

	item = kmalloc(sizeof(struct list_item), GFP_KERNEL);

	item->device = ddata;

	list_add_tail(&item->links, &list);

	contDev++;

	kfree(devName);

	ddata->cur_led_state = 0;
	gpiod_set_value(ddata->led_gpio, ddata->cur_led_state);

	return 0;

	err_handle:

		for (j = 0; j < i; j++)
    			gpiod_put(gpio_descriptors[j]);

		kfree(devName);
		kfree(ddata);

 		return ret;
	error_alloc_state:
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
	
	kfree(devName);

	return ret;
}

/*
 * This function is called when the module is unloaded
 */
void exit_modcontrol_module(void)
{
	struct device* dev;
	struct list_head *curr = NULL;
	struct list_head *aux = NULL;
	struct list_item *item = NULL;

	list_for_each_safe(curr, aux, &list){
		item = list_entry(curr, struct list_item, links);
		list_del(curr);
		free_irq(item->device->gpio_button_irqn, item->device);
		gpiod_put(item->device->button_gpio);
		gpiod_put(item->device->led_gpio);
		dev= class_find_device_by_devt(class, item->device->major_minor);
		kfree(item->device);
		device_destroy(class, dev->devt);
		kfree(item);
	}

	    class_destroy(class);

    	/* Destroy chardev */
    	if (chardev)
        	cdev_del(chardev);

    	/*
     	* Release major minor pair
     	*/
    	unregister_chrdev_region(start, 3);

	remove_proc_entry("modcontrol", NULL);
}

/*
 * Called when a process tries to open the device file, like
 * "cat /dev/chardev"
 */
static int device_open(struct inode *inode, struct file *file)
{
	struct device* device;
    	struct led_switch* ddata;
    	/* Retrieve device from major minor of the device file */
    	device = class_find_device_by_devt(class, inode->i_rdev);

    	if (!device)
        	return -ENODEV;

	/* Retrieve driver's private data from device */
    	ddata = dev_get_drvdata(device);

    	if (!ddata)
        	return -ENODEV;

    	if (ddata->Device_Open)
        	return -EBUSY;

	ddata->Device_Open++;

	file->private_data = ddata;

	try_module_get(THIS_MODULE);
	
	return SUCCESS;
}

/*
 * Called when a process closes the device file.
 */
static int device_release(struct inode *inode, struct file *file)
{
    struct led_switch* ddata = file->private_data;

    if (ddata == NULL)
        return -ENODEV;

    ddata->Device_Open--;       /* We're now ready for our next caller */

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
                           char *buf,    /* buffer to fill with data */
                           size_t len,   /* length of the buffer     */
                           loff_t * off)
{
	int nr_bytes = sizeof(char) * 3;
	char *kbuf = kmalloc(3, GFP_KERNEL);
	struct led_switch* ddata = filp->private_data;
	memset(kbuf, 0, 3);

    	if (ddata == NULL)
        	return -ENODEV;

	if((*off) > 0){
		kfree(kbuf);
		return 0;
	}

	spin_lock_irqsave(&ddata->sp, ddata->flags);
	sprintf(kbuf, "%i\n", ddata->cur_led_state);
	if(len < nr_bytes){
		kfree(kbuf);
		spin_unlock_irqrestore(&ddata->sp, ddata->flags);
		return -ENOSPC;
	}
	if(copy_to_user(buf, kbuf, nr_bytes)){
		kfree(kbuf);
		spin_unlock_irqrestore(&ddata->sp, ddata->flags);
		return -EINVAL;
	}
	spin_unlock_irqrestore(&ddata->sp, ddata->flags);
	(*off) += len;
	kfree(kbuf);
	
	return nr_bytes;
}

/*
 * Called when a process writes to dev file: echo "hi" > /dev/chardev
 */
static ssize_t
device_write(struct file *filp, const char *buf, size_t len, loff_t * off)
{
	int n;
	char *kbuf = kmalloc(2, GFP_KERNEL);
	struct led_switch* ddata = filp->private_data;

    	if (ddata == NULL)
        	return -ENODEV;	
	if(len > (sizeof(char)*3)) {
		printk(KERN_INFO "modcontrol: not enough space\n");
		kfree(kbuf);
		return -ENOSPC;
	}
	if(copy_from_user(kbuf, buf, len)){
		kfree(kbuf);
		return -EINVAL;
	}
	kbuf[len] = '\0';

	spin_lock_irqsave(&ddata->sp, ddata->flags);
	if(sscanf(kbuf, "%i", &n) == 1){
		if(n == 0 || n == 1){

			ddata->cur_led_state = n;
			gpiod_set_value(ddata->led_gpio, ddata->cur_led_state);
		}
		else {
			kfree(kbuf);
			spin_unlock_irqrestore(&ddata->sp, ddata->flags);
			return -EINVAL;
		}
	}

	else {
		kfree(kbuf);
		spin_unlock_irqrestore(&ddata->sp, ddata->flags);
		return -EINVAL;
	}

	spin_unlock_irqrestore(&ddata->sp, ddata->flags);

	*off += len;
	kfree(kbuf);
	return len;
}

module_init(init_modcontrol_module);
module_exit(exit_modcontrol_module);
