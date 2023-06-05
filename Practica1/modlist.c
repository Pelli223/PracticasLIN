#include <linux/module.h>
#include <linux/kernel.h>
#include<linux/slab.h>
#include<linux/string.h>
#include<linux/proc_fs.h>
#include<linux/uaccess.h>
#include<linux/list.h>
MODULE_LICENSE("GLP");

#define BUFFER_LENGTH 	PAGE_SIZE

static struct proc_dir_entry *proc_entry;
static struct list_head list;

struct list_item{
	int dato;
	struct list_head links;
};

static ssize_t modlist_write (struct file *filp, const char __user *buf, size_t len, loff_t *off){
	int n;
	int avalible_space = BUFFER_LENGTH - 1;
	char *kbuf = (char*) kmalloc(sizeof(char)*BUFFER_LENGTH, GFP_KERNEL);
	struct list_item *item = NULL;
	if((*off) > 0){
		kfree(kbuf);
		return 0;
	}
	if(len > avalible_space) {
		printk(KERN_INFO "modlist: not enough space\n");
		kfree(kbuf);
		return -ENOSPC;
	}
	if(copy_from_user(kbuf, buf, len)){
		kfree(kbuf);
		return -EINVAL;
	}
	kbuf[len] = '\0';
	if(sscanf(kbuf, "add %i", &n) == 1){
		item = kmalloc(sizeof(struct list_item), GFP_KERNEL);
		item->dato = n;
		list_add_tail(&item->links, &list);
	}
	else if(sscanf(kbuf, "remove %i", &n) == 1){
		struct list_head *curr = NULL;
		struct list_head *aux = NULL;
		list_for_each_safe(curr, aux, &list){
			item = list_entry(curr, struct list_item, links);
			if(item->dato == n){
				list_del(curr);
				kfree(item);
			}
		}
	}
	else if(strcmp(kbuf, "cleanup\n") == 0){
		struct list_head *curr = NULL;
		struct list_head *aux = NULL;
		list_for_each_safe(curr, aux, &list){
			item = list_entry(curr, struct list_item, links);
			list_del(curr);
			kfree(item);
		}
	}
	else{
		kfree(kbuf);
		return -EINVAL;
	}
	*off += len;
	kfree(kbuf);

	return len;
}

static ssize_t modlist_read(struct file *filp, char __user *buf, size_t len, loff_t *off){
	int nr_bytes = 0;
	char *kbuf = (char*) kmalloc(sizeof(char)*BUFFER_LENGTH, GFP_KERNEL);
	struct list_item *item = NULL;
	struct list_head *curr = NULL;
	if((*off) > 0){
		kfree(kbuf);
		return 0;
	}
	list_for_each(curr, &list) {
		nr_bytes += sizeof(int) + sizeof(char);
		if(nr_bytes > BUFFER_LENGTH-1){
			kfree(kbuf);
			return -ENOSPC;
		}
		item = list_entry(curr, struct list_item, links);
		sprintf(&kbuf[strlen(kbuf)], "%i\n", item->dato);

	}
	if(len < nr_bytes){
		kfree(kbuf);
		return -ENOSPC;
	}
	if(copy_to_user(buf, kbuf, nr_bytes)){
		kfree(kbuf);
		return -EINVAL;
	}
	(*off) += len;
	kfree(kbuf);
	return nr_bytes;
}

static struct proc_ops pops = {
	.proc_read = modlist_read,
	.proc_write = modlist_write,
};

int init_modlist_module( void ){
	int ret = 0;
	INIT_LIST_HEAD(&list);
	proc_entry = proc_create("modlist", 0666, NULL, &pops);
	if(proc_entry == NULL){
		ret = -ENOMEM;
		kfree(&list);
		printk(KERN_INFO "ERROR: Cant create module\n");
	}
	else{
		printk(KERN_INFO "Modulo modlist cargado\n");
	}
	return ret;
}

void exit_modlist_module( void ){
	struct list_head *curr = NULL;
	struct list_head *aux = NULL;
	struct list_item *item;
	remove_proc_entry("modlist", NULL);
	list_for_each_safe(curr, aux, &list){
		list_del(curr);
		item = list_entry(curr, struct list_item, links);
		kfree(item);
	}
	printk(KERN_INFO "Modulo modlist descargado\n");	
}

module_init(init_modlist_module);
module_exit(exit_modlist_module);
