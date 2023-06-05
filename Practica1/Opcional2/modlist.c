#include <linux/module.h>
#include <linux/kernel.h>
#include<linux/slab.h>
#include<linux/string.h>
#include<linux/proc_fs.h>
#include<linux/uaccess.h>
#include<linux/list.h>
#include<linux/seq_file.h>
MODULE_LICENSE("GLP");

#define BUFFER_LENGTH PAGE_SIZE

static struct proc_dir_entry *proc_entry;
static struct list_head list;
static int cont = 0;

struct list_item{
	int dato;
	struct list_head links;
};

static void *modlist_seq_start(struct seq_file *s, loff_t *pos){
	if(*pos == 0 && cont != 0)
		return list.next;
	
	*pos = 0;
	return NULL;
}

static void *modlist_seq_next(struct seq_file *s, void *v, loff_t *pos){
	struct list_head *tmp_list = (struct list_head *) v;
	tmp_list = tmp_list->next;
	(*pos)++;
	if(*pos == cont)
		return NULL;

	return tmp_list;
}

static void modlist_seq_stop(struct seq_file *s, void *v){

}

static int modlist_seq_show(struct seq_file *s, void *v){
	struct list_item *item= list_entry((struct list_head*) v, struct list_item, links);
	seq_printf(s, "%i\n", item->dato);

	return 0;
}

static struct seq_operations modlist_seq_ops = {
	.start = modlist_seq_start,
	.next = modlist_seq_next,
	.stop = modlist_seq_stop,
	.show = modlist_seq_show,
};

static int modlist_seq_open(struct inode *inode, struct file *file){
	return seq_open(file, &modlist_seq_ops);
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
	if(copy_from_user(kbuf, buf, BUFFER_LENGTH))
		return -EINVAL;
	kbuf[len] = '\0';
	if(sscanf(kbuf, "add %i", &n) == 1){
		item = kmalloc(sizeof(struct list_item), GFP_KERNEL);
		item->dato = n;
		list_add_tail(&item->links, &list);
		cont ++;
	}
	else if(sscanf(kbuf, "remove %i", &n) == 1){
		struct list_head *curr = NULL;
		struct list_head *aux = NULL;
		list_for_each_safe(curr, aux, &list){
			item = list_entry(curr, struct list_item, links);
			if(item->dato == n){
				list_del(curr);
				kfree(item);
				cont --;
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
		cont = 0;
	}
	else{
		kfree(kbuf);
		return -EINVAL;
	}
	*off += len;
	kfree(kbuf);

	return len;
}

static struct proc_ops pops = {
	.proc_open = modlist_seq_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = seq_release,
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
