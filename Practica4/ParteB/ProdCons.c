#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>  /* for copy_to_user */
#include <linux/miscdevice.h>
#include<linux/slab.h>
#include<linux/semaphore.h>
#include<linux/kfifo.h>

MODULE_DESCRIPTION("ChardevMisc Kernel Module - FDI-UCM");
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
#define DEVICE_NAME "prodcons"  /* Dev name as it appears in /proc/devices   */
#define CLASS_NAME "cool"
#define BUF_LEN 80      /* Max length of the message from the device */
#define MAX_ITEMS_CBUF 4

struct kfifo cbuf;
struct semaphore elementos,huecos, mtx;

static struct file_operations fops = {
    .read = device_read,
    .write = device_write,
    .open = device_open,
    .release = device_release
};


static struct miscdevice misc_prodcons = {
    .minor = MISC_DYNAMIC_MINOR,    /* kernel dynamically assigns a free minor# */
    .name = DEVICE_NAME, /* when misc_register() is invoked, the kernel
                        * will auto-create device file as /dev/chardev ;
                        * also populated within /sys/class/misc/ and /sys/devices/virtual/misc/ */
    .mode = 0666,     /* ... dev node perms set as specified here */
    .fops = &fops,    /* connect to this driver's 'functionality' */
};

/*
 * This function is called when the module is loaded
 */
int init_module(void)
{
    int major;      /* Major number assigned to our device driver */
    int minor;      /* Minor number assigned to the associated character device */
    int ret;
    struct device *device;

	if (kfifo_alloc(&cbuf,MAX_ITEMS_CBUF*sizeof(int),GFP_KERNEL))
		return -ENOMEM;

	sema_init(&elementos,0);
	sema_init(&huecos,MAX_ITEMS_CBUF);
	sema_init(&mtx, 1);

    ret = misc_register(&misc_prodcons);

    if (ret) {
        pr_err("Couldn't register misc device\n");
        return ret;
    }

    device = misc_prodcons.this_device;

    /* Access devt field in device structure to retrieve (major,minor) */
    major = MAJOR(device->devt);
    minor = MINOR(device->devt);

    dev_info(device, "I was assigned major number %d. To talk to\n", major);
    dev_info(device, "the driver try to cat and echo to /dev/%s.\n", DEVICE_NAME);
    dev_info(device, "Remove the module when done.\n");

    return 0;
}

/*
 * This function is called when the module is unloaded
 */
void cleanup_module(void)
{
    kfifo_free(&cbuf);
    misc_deregister(&misc_prodcons);
    pr_info("Chardev misc driver deregistered. Bye\n");
}

/*
 * Called when a process tries to open the device file, like
 * "cat /dev/chardev"
 */
static int device_open(struct inode *inode, struct file *file)
{
    //if (Device_Open)
        //return -EBUSY;

    //Device_Open++;

    /* Increment the module's reference counter */
    try_module_get(THIS_MODULE);

    return SUCCESS;
}

/*
 * Called when a process closes the device file.
 */
static int device_release(struct inode *inode, struct file *file)
{
    //Device_Open--;      /* We're now ready for our next caller */

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
                           char *buff,    /* buffer to fill with data */
                           size_t len,   /* length of the buffer     */
                           loff_t * off)
{
	char *kbuf = kmalloc(MAX_ITEMS_CBUF+1, GFP_KERNEL);
    	int nr_bytes=0;
    	int val, ret;
    	if ((*off)>0){
		kfree(kbuf);
        	return 0;
	}
    	if (down_interruptible(&elementos)){
		kfree(kbuf);
		return -EINTR;
	}
	if (down_interruptible(&mtx)){
		up(&elementos);
		return -EINTR;
	}
	/* Extraer el primer entero del buffer */
	ret = kfifo_out(&cbuf,&val,sizeof(int));
	if(ret < 0){
		kfree(kbuf);
		return -EINVAL;
	}
	up(&mtx);
	up(&huecos);
	nr_bytes=sprintf(kbuf,"%i\n",val);
	if(copy_to_user(buff, kbuf, nr_bytes)){
		kfree(kbuf);
		return -EINVAL;
	}

	(*off) += len;

    	return nr_bytes;
}

/*
 * Called when a process writes to dev file: echo "hi" > /dev/chardev
 */
static ssize_t
device_write(struct file *filp, const char *buff, size_t len, loff_t * off)
{
	char *kbuf = kmalloc(MAX_ITEMS_CBUF+1, GFP_KERNEL);
	int val=0;
	if(copy_from_user(kbuf, buff, len)){
		kfree(kbuf);
		return -EINVAL;
	}
	if(sscanf(kbuf, "%i", &val) != 1){
		kfree(kbuf);
		return -EINVAL;
	}
		
	if (down_interruptible(&huecos)){
		kfree(kbuf);
		return -EINTR;
	}
	if (down_interruptible(&mtx)) {
		up(&huecos);
		return -EINTR;
	}

	kfifo_in(&cbuf,&val,sizeof(int));
	up(&mtx);
	up(&elementos);

	return len;
}
