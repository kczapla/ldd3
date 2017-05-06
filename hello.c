#include <linux/module.h>
#include <linux/kernel.h>
MODULE_LICENSE("Dual BSD/GPL");


static int hello_init(void)
{
 printk(KERN_INFO "init_module() called\n");
 return 0;
}

static void hello_exit(void)
{
 printk(KERN_INFO "cleanup_module() called\n");
}

module_init(hello_init);
module_exit(hello_exit);
