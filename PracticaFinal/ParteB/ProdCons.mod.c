#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);

__visible struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif

static const struct modversion_info ____versions[]
__used
__attribute__((section("__versions"))) = {
	{ 0x54d4519d, __VMLINUX_SYMBOL_STR(module_layout) },
	{ 0x78e6eaf2, __VMLINUX_SYMBOL_STR(cdev_del) },
	{ 0x6af8b224, __VMLINUX_SYMBOL_STR(kobject_put) },
	{ 0xdb760f52, __VMLINUX_SYMBOL_STR(__kfifo_free) },
	{ 0x7485e15e, __VMLINUX_SYMBOL_STR(unregister_chrdev_region) },
	{ 0x27e1a049, __VMLINUX_SYMBOL_STR(printk) },
	{ 0xddab292d, __VMLINUX_SYMBOL_STR(cdev_add) },
	{ 0x76f80078, __VMLINUX_SYMBOL_STR(cdev_init) },
	{ 0x19f9547, __VMLINUX_SYMBOL_STR(cdev_alloc) },
	{ 0x29537c9e, __VMLINUX_SYMBOL_STR(alloc_chrdev_region) },
	{ 0x97868aef, __VMLINUX_SYMBOL_STR(__kfifo_alloc) },
	{ 0xc671e369, __VMLINUX_SYMBOL_STR(_copy_to_user) },
	{ 0x91715312, __VMLINUX_SYMBOL_STR(sprintf) },
	{ 0x13d0adf7, __VMLINUX_SYMBOL_STR(__kfifo_out) },
	{ 0xdb7305a1, __VMLINUX_SYMBOL_STR(__stack_chk_fail) },
	{ 0x37a0cba, __VMLINUX_SYMBOL_STR(kfree) },
	{ 0x78e739aa, __VMLINUX_SYMBOL_STR(up) },
	{ 0xf23fcb99, __VMLINUX_SYMBOL_STR(__kfifo_in) },
	{ 0xe0afe59e, __VMLINUX_SYMBOL_STR(down_interruptible) },
	{ 0x20c55ae0, __VMLINUX_SYMBOL_STR(sscanf) },
	{ 0xb5419b40, __VMLINUX_SYMBOL_STR(_copy_from_user) },
	{ 0x88db9f48, __VMLINUX_SYMBOL_STR(__check_object_size) },
	{ 0xcd17f66c, __VMLINUX_SYMBOL_STR(kmem_cache_alloc_trace) },
	{ 0xc8dcbb5a, __VMLINUX_SYMBOL_STR(kmalloc_caches) },
	{ 0xe78d42a, __VMLINUX_SYMBOL_STR(try_module_get) },
	{ 0x73755107, __VMLINUX_SYMBOL_STR(module_put) },
	{ 0xbdfb6dbb, __VMLINUX_SYMBOL_STR(__fentry__) },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=";


MODULE_INFO(srcversion, "46634D844A47C7573532C98");
