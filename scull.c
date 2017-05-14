#include <asm/uaccess.h>
#include <linux/cdev.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include "scull.h"

MODULE_LICENSE("Dual BSD/GPL");

int scull_major = SCULL_MAJOR;
int scull_minor = SCULL_MINOR;
int scull_nr_devs = SCULL_NR_DEVS;
int scull_quantum = SCULL_QUANTUM;
int scull_qset = SCULL_QSET;


struct file_operations scull_fops = {
    .owner = THIS_MODULE,
    .read = scull_read,
    .write = scull_write,
    .open = scull_open,
};


static int scull_init(void)
{
    int i, result;
    
    result = alloc_chrdev_region(&dev_no, scull_minor, scull_nr_devs, "scull");
    if (result < 0) {
        printk(KERN_WARNING "scull: can't get major %d\n",scull_major);
        return result;
    }
    scull_major = MAJOR(dev_no);

    scull_devs = kmalloc(scull_nr_devs * sizeof(struct scull_dev), GFP_KERNEL);
    if (!scull_devs) {
        result = -ENOMEM;
        goto fail;
    }

    memset(scull_devs, 0, scull_nr_devs * sizeof(struct scull_dev));

    for (i = 0; i < scull_nr_devs; i++) {
        scull_devs[i].quantum = scull_quantum;
        scull_devs[i].data = NULL;
        scull_devs[i].qset = scull_qset;
        sema_init(&scull_devs[i].sem, 1);
        scull_setup_cdev(&scull_devs[i], i);
    }
    printk(KERN_INFO "init_module() called\n");
    return 0;

    fail:
      scull_cleanup_module();
      return result;
}

struct scull_qset *scull_follow(struct scull_dev *dev, int n)
{
    // init first node
    struct scull_qset *qset = dev->data;
    printk(KERN_WARNING "before init scull_follow->qset: %p", (void *) qset);
    if (!qset) {
        printk(KERN_WARNING "init first node");
        qset = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
        printk(KERN_WARNING "after init scull_follow->qset: %p", (void *) qset);
        memset(qset, 0, sizeof(struct scull_qset));
        dev->data = qset;
        return qset;
    }

    while (n--) {
        if (!qset->next) {
            qset->next = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
            memset(qset->next, 0, sizeof(struct scull_qset));
        }
        qset = qset->next;
        continue;
    }
    return qset;
}

static void scull_cleanup_module(void)
{
    int i;
    printk(KERN_INFO "cleanup_module() called\n");
    unregister_chrdev_region(dev_no, scull_nr_devs);
    for (i = 0; i < scull_nr_devs; i++) {
        scull_trim(&scull_devs[i]);
    }
}

static void scull_exit(void)
{
    scull_cleanup_module();
    printk(KERN_INFO "exit_module() called\n");
}

int scull_open(struct inode *inode, struct file *filp)
{
    struct scull_dev *dev; // device information

    dev = container_of(inode->i_cdev, struct scull_dev, cdev);
    filp->private_data = dev; // for other methods

    if (  (filp->f_flags & O_ACCMODE) == O_WRONLY ) {
        scull_trim(dev); // ignore errors
    }
    return 0; //success
}

ssize_t scull_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    struct scull_dev *dev = filp->private_data;
    struct scull_qset *dptr;
    int quantum = dev->quantum, qset = dev->qset;
    int itemsize = quantum * qset; // how many bytes in the list item
    int item, s_pos, q_pos, rest;
    ssize_t retval = 0;

    if ( down_interruptible(&dev->sem) )
        return -ERESTARTSYS;
    if ( *f_pos >= dev->size )
        goto out;
    if ( *f_pos + count > dev->size )
        count = dev->size - *f_pos;

    // find listitem, qset index, and offset in the quantum
    item = (long)*f_pos / itemsize; 
    rest = (long)*f_pos % itemsize;
    s_pos = rest / quantum; q_pos = rest % quantum;

    // follow the lis tup to the right position (defined elsewhere)
    dptr = scull_follow(dev, item);

    if ( dptr == NULL || !dptr->data || !dptr->data[s_pos] )
        goto out; //don't fill holes

    // read only up to the end of this quantum
    if ( count > quantum - q_pos )
        count = quantum - q_pos;

    if ( copy_to_user(buf, dptr->data[s_pos] + q_pos, count) ) {
        retval = -EFAULT;
        goto out;
    }
    *f_pos += count;
    retval = count;

    out:
        up(&dev->sem);
        return retval;
}

int scull_release(struct inode *inode, struct file *filp)
{
    return 0;
}

loff_t scull_llseek(struct file *filp, loff_t off, int whence)
{
    struct scull_dev *dev = filp->private_data;
    loff_t newpos;

    switch(whence) {
      case 0: /* SEEK_SET */
        newpos = off;
        break;

      case 1: /* SEEK_CUR */
        newpos = filp->f_pos + off;
        break;

      case 2: /* SEEK_END */
        newpos = dev->size + off;
        break;

      default: /* can't happen */
        return -EINVAL;
    }
    if (newpos<0) return -EINVAL;
    filp->f_pos = newpos;
    return newpos;
}

ssize_t scull_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    struct scull_dev *dev = filp->private_data;
    struct scull_qset *dptr;
    int quantum = dev->quantum, qset = dev->qset;
    int itemsize = quantum * qset;
    int item, s_pos, q_pos, rest;
    ssize_t retval = -ENOMEM; // value used in "goto out" statements


    if ( down_interruptible(&dev->sem) )
        return -ERESTARTSYS;

    // find listitem, qset index, and offset in the quantum
    item = (long)*f_pos / itemsize; // one item per device. Device mapped on qset's lists. On device has 4kk size. Always rounded down.
    rest = (long)*f_pos % itemsize; // Offset in the qset data field.
    s_pos = rest / quantum; q_pos = rest % quantum;

    // follow the lis tup to the right position (defined elsewhere)
    dptr = scull_follow(dev, item);

    if ( dptr == NULL )
        goto out;
    if ( !dptr->data ) {
        dptr->data = kmalloc(qset * sizeof(char *), GFP_KERNEL);
        if ( !dptr->data )
            goto out;
        memset(dptr->data, 0, qset * sizeof(char *));
    }
    if ( !dptr->data[s_pos] ) {
        dptr->data[s_pos] = kmalloc(quantum, GFP_KERNEL);
        if ( !dptr->data[s_pos] )
            goto out;
    }

    // write onlu up to the end of this quantum
    if ( count > quantum - q_pos )
        count = quantum - q_pos;

    if ( copy_from_user(dptr->data[s_pos] + q_pos, buf, count) ) {
        retval = EFAULT;
        goto out;
    }
    *f_pos += count;
    retval = count;

    // update the size
    if ( dev->size < *f_pos )
        dev->size = *f_pos;

    out:
        up(&dev->sem);
        return retval;
}

int scull_trim(struct scull_dev *dev)
{
    struct scull_qset *next, *dptr;
    int qset = dev->qset; // "dev" is not null
    int i;
    for (dptr = dev->data; dptr; dptr = next) { // all list items
        if ( dptr->data ) {
            for (i = 0; i < qset; i++)
                kfree(dptr->data[i]);
            kfree(dptr->data);
            dptr->data = NULL;
        }
        next = dptr->next;
        kfree(dptr);
    }
    dev->size = 0;
    dev->quantum = scull_quantum;
    dev->qset = scull_qset;
    dev->data = NULL;
    return 0;
}

static void scull_setup_cdev(struct scull_dev *dev, int index)
{
    int err, devno = MKDEV(scull_major, scull_minor + index);

    // scull's file operation structure
    cdev_init(&dev->cdev, &scull_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &scull_fops;
    err = cdev_add(&dev->cdev, devno, 1);
    if ( err )
    printk(KERN_NOTICE "Error %d adding scull%d", err, index);
}

module_init(scull_init);
module_exit(scull_exit);
