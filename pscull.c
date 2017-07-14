#include <asm/uaccess.h>
#include <linux/cdev.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/proc_fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include "pscull.h"

MODULE_LICENSE("Dual BSD/GPL");

int pscull_major = PSCULL_MAJOR;
int pscull_minor = PSCULL_MINOR;
int pscull_nr_devs = PSCULL_NR_DEVS;
int pscull_buffer_size = PSCULL_BUFFER_SIZE; 

struct file_operations pscull_fops = {
	.owner = THIS_MODULE,
	.fasync = pscull_fasync,
	.read = pscull_read,
	.write = pscull_write,
	.poll = pscull_poll,
	.llseek = no_llseek,
	.open = pscull_open,
	.release = pscull_release,
};

static int pscull_init(void)
{
	int i, result;
    
	result = alloc_chrdev_region(&dev_no, pscull_minor, pscull_nr_devs, "pscull");
	if (result < 0) {
		printk(KERN_WARNING "pscull: can't get major %d\n",pscull_major);
		return result;
    }
	pscull_major = MAJOR(dev_no);
	pscull_devs = kmalloc(pscull_nr_devs * sizeof(struct pscull_dev), GFP_KERNEL);
	if (!pscull_devs) {
		result = -ENOMEM;
		goto fail;
	}
	memset(pscull_devs, 0, pscull_nr_devs * sizeof(struct pscull_dev));
	for (i = 0; i < pscull_nr_devs; i++) {
		pscull_devs[i].buffersize = pscull_buffer_size;
		pscull_devs[i].buffer = kmalloc(pscull_buffer_size * sizeof(char), GFP_KERNEL);
		pscull_devs[i].end = pscull_devs[i].buffer + pscull_devs[i].buffersize - 1;
		pscull_devs[i].rp = pscull_devs[i].buffer;
		pscull_devs[i].wp = pscull_devs[i].buffer;
		init_waitqueue_head(&pscull_devs[i].inq);
		init_waitqueue_head(&pscull_devs[i].outq);
		sema_init(&pscull_devs[i].sem, 1);
		pscull_setup_cdev(&pscull_devs[i], i);
	}

	printk(KERN_INFO "init_module() called\n");
	return 0;
	
	fail:
		pscull_cleanup_module();
		return result;
}

static void pscull_cleanup_module(void)
{
	int i;
	printk(KERN_INFO "cleanup_module() called\n");
	for (i = 0; i < pscull_nr_devs; i++) {
		kfree(pscull_devs[i].buffer);
		pscull_devs[i].end = NULL;
		pscull_devs[i].rp = NULL;
		pscull_devs[i].wp = NULL;
	}
	unregister_chrdev_region(dev_no, pscull_nr_devs);
}

static void pscull_exit(void)
{
    	pscull_cleanup_module();
	printk(KERN_INFO "exit_module() called\n");
}

static int pscull_fasync(int fd, struct file *filp, int mode)
{
	printk(KERN_INFO "pscull_fasync called\n");
	struct pscull_dev *dev = filp->private_data;
	return fasync_helper(fd, filp, mode, &dev->async_queue);
}

int pscull_open(struct inode *inode, struct file *filp)
{
    	struct pscull_dev *dev; // device information

    	dev = container_of(inode->i_cdev, struct pscull_dev, cdev);
    	filp->private_data = dev; // for other methods

    	return 0; //success
}

static unsigned int pscull_poll(struct file *filp, poll_table *wait)
{
	struct pscull_dev *dev = filp->private_data;
	unsigned int mask = 0;
	down(&dev->sem);
	poll_wait(filp, &dev->inq, wait);
	poll_wait(filp, &dev->outq, wait);
	if (dev->rp != dev->wp)
		mask |= POLLIN | POLLRDNORM;
	if (spacefree(dev))
		mask |= POLLOUT | POLLWRNORM;
	up(&dev->sem);
	return mask;
}

ssize_t pscull_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	struct pscull_dev *dev = filp->private_data;
	printk(KERN_INFO "read: before sleep dev->wp: %p, dev->rp: %p", dev->wp, dev->rp);
	if (down_interruptible(&dev->sem))
		return -ERESTARTSYS;
	while (dev->rp == dev->wp) {
		up(&dev->sem);
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		//printk(KERN_INFO "reading: going to sleep\n");
    		printk(KERN_WARNING "\"%s\" reading: going to sleep\n", current->comm);
		if (wait_event_interruptible(dev->inq, (dev->rp != dev->wp)))
			return -ERESTARTSYS;
		if (down_interruptible(&dev->sem))
			return -ERESTARTSYS;
	}
	printk(KERN_INFO "read; count before if stat: %lu\n", count);
	printk(KERN_INFO "read: dev->wp: %p, dev->end: %p", dev->wp, dev->end);
	printk(KERN_INFO "read: after sleep dev->wp: %p, dev->rp: %p", dev->wp, dev->rp);
	if (dev->wp > dev->rp) {
		count = min(count, (size_t)(dev->wp - dev->rp));
	}
	else
		count = min(count, (size_t)(dev->end - dev->rp));
	printk(KERN_INFO "read; count after if stat: %lu\n", count);
	if (copy_to_user(buf, dev->rp, count)) {
		up (&dev->sem);
		return -EFAULT;
	}
	dev->rp += count;
	printk(KERN_INFO "read: dev->rp: %p, dev->wp: %p", dev->rp, dev->wp);
	if (dev->rp == dev->end)
		dev->rp = dev->buffer;
	up(&dev->sem);
	wake_up_interruptible(&dev->outq);
	PDEBUG("\"%s\" did read %li bytes\n", current->comm, (long)count);
	return count;
}

int pscull_release(struct inode *inode, struct file *filp)
{
    return 0;
}

// How much space is free
static int spacefree(struct pscull_dev *dev)
{
	if (dev->rp == dev->wp)
		return dev->buffersize - 1;
	return ((dev->rp + dev->buffersize - dev->wp) % dev->buffersize) - 1;
}

ssize_t pscull_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	struct pscull_dev *dev = filp->private_data;

	printk(KERN_INFO "write: dev->rp: %p, dev->wp: %p", dev->rp, dev->wp);
	if ( down_interruptible(&dev->sem) )
		return -ERESTARTSYS;
	while (spacefree(dev) == 0) {
		up(&dev->sem);
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		printk(KERN_INFO "\"%s\" writing: going to sleep\n", current->comm);
		if (wait_event_interruptible(dev->outq, (spacefree(dev) > 0)))
			return -ERESTARTSYS;
		if (down_interruptible(&dev->sem))
			return -ERESTARTSYS;
	}
	count = min(count, (size_t)spacefree(dev));
	printk(KERN_INFO "write: count before accepting %lu\n", count);
	printk(KERN_INFO "write: dev->wp: %p, dev->end: %p", dev->wp, dev->end);
	if (dev->wp >= dev->rp)
		count = min(count, (size_t)(dev->end - dev->wp));
	else
		count = min(count, (size_t)(dev->rp - dev->wp - 1));
	printk(KERN_INFO "Going to accept %li bytes to %p from %p\n", (long)count, dev->wp, buf);
	if (copy_from_user(dev->wp, buf, count)) {
		up(&dev->sem);
		return -EFAULT;
	}
	dev->wp += count;
	if (dev->wp == dev->end)
		dev->wp = dev->buffer;
	up(&dev->sem);
	wake_up_interruptible(&dev->inq);
	if (dev->async_queue)
		kill_fasync(&dev->async_queue, SIGIO, POLL_IN);
	printk(KERN_INFO "\"%s\" did write %li bytes\n", current->comm, (long)count);
	return count;
}

static void pscull_setup_cdev(struct pscull_dev *dev, int index)
{
	int err, devno = MKDEV(pscull_major, pscull_minor + index);
	// pscull's file operation structure
	cdev_init(&dev->cdev, &pscull_fops);
	dev->cdev.owner = THIS_MODULE;
	printk(KERN_INFO "dev->cdev.ops = %p", (void *) &pscull_fops);
	dev->cdev.ops = &pscull_fops;
	err = cdev_add(&dev->cdev, devno, 1);
	if ( err )
		printk(KERN_NOTICE "Error %d adding pscull%d", err, index);
}

module_init(pscull_init);
module_exit(pscull_exit);
