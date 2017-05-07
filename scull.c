#include <asm/uaccess.h>
#include <linux/cdev.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include "scull.h"

//MODULE_LICENSE("Dual BSD/GPL");

int get_major_version(void)
{
    int result;
    if (scull_major) {
        dev = MKDEV(scull_major, scull_minor);
        result = register_chrdev_region(dev, scull_nr_devs, "scull");
    } else {
        result = alloc_chrdev_region(&dev, scull_minor, scull_nr_devs, "scull");
        scull_major = MAJOR(dev);
    }
    if (result < 0) {
        printk(KERN_WARNING "scull: can't get major %d\n", scull_major);
    }
    return result;
}

static int scull_init(void)
{
    int scull_major = SCULL_MAJOR;
    int scull_minor = SCULL_MINOR;
    int scull_nr_devs = SCULL_NR_DEVS;
    int scull_quantum = SCULL_QUANTUM;
    int scull_qset = SCULL_QSET;
    printk(KERN_INFO "init_module() called\n");
    return 0;
}

static void scull_exit(void)
{
    printk(KERN_INFO "cleanup_module() called\n");
}

int scull_open(struct inode *inode, struct file *filp)
{
    struct scull_dev *dev; // device information

    dev = container_of(inode->i_cdev, struct scull_dev, cdev);
    filp->private_data = dev; // for other methods

    if ( (filp->f_flags & O_ACCMODE) == O_WRONLY) {
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

    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;
    if (*f_pos >= dev->size)
        goto out;
    if (*f_pos + count > dev->size)
        count = dev->size - *f_pos;

    // find listitem, qset index, and offset in the quantum
    item = (long)*f_pos / itemsize; // one item per device. Device mapped on qset's lists. On device has 4kk size. Always rounded down.
    rest = (long)*f_pos % itemsize; // Offset in the qset data field.
    s_pos = rest / quantum; q_pos = rest % quantum;

    // follow the lis tup to the right position (defined elsewhere)
    dptr = scull_follow(dev, item);

    if (dptr == NULL || !dptr->data || !dptr->data[s_pos])
        goto out; //don't fill holes

    // read only up to the end of this quantum
    if (count > quantum - q_pos)
        count = quantum - q_pos;

    if (copy_to_user(buf, dptr->data[s_pos] + q_pos, count)) {
        retval = -EFAULT;
        goto out;
    }
    *f_pos = count;
    retval = count;

    out:
        up(&dev->sem);
        return retval;
}

ssize_t scull_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    struct scull_dev *dev = filp->private_data;
    struct scull_qset *dptr;
    int quantum = dev->quantum, qset = dev->qset;
    int itemsize = quantum * qset;
    int item, s_pos, q_pos, rest;
    ssize_t retval = -ENOMEM; // value used in "goto out" statements


    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;

    // find listitem, qset index, and offset in the quantum
    item = (long)*f_pos / itemsize; // one item per device. Device mapped on qset's lists. On device has 4kk size. Always rounded down.
    rest = (long)*f_pos % itemsize; // Offset in the qset data field.
    s_pos = rest / quantum; q_pos = rest % quantum;

    // follow the lis tup to the right position (defined elsewhere)
    dptr = scull_follow(dev, item);

    if (dptr == NULL)
        goto out;
    if (!dptr->data) {
        dptr->data = kmalloc(qset *sizeof(char *), GFP_KERNEL);
        if (!dptr->data)
            goto out;
        memset(dptr->data, 0, qset * sizeof(char *));
    }
    if (!dptr->data[s_pos]) {
        dptr->data[s_pos] = kmalloc(quantum, GFP_KERNEL);
        if (!dptr->data[s_pos])
            goto out;
    }

    // write onlu up to the end of this quantum
    if (count > quantum - q_pos)
        count = quantum - q_pos;

    if (copy_from_user(dptr->data[s_pos] + q_pos, buf, count)) {
        retval = EFAULT;
        goto out;
    }
    *f_pos += count;
    retval = count;

    // update the size
    if (dev->size < *f_pos)
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
        if (dptr->data) {
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
    struct file_operations scull_fops = {
        .owner = THIS_MODULE,
        .read = scull_read,
        .write = scull_write,
        .open = scull_open,
    };
    cdev_init(&dev->cdev, &scull_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &scull_fops;
    err = cdev_add(&dev->cdev, devno, 1);
    if (err)
    printk(KERN_NOTICE "Error %d adding scull%d", err, index);
}

// module_init(scull_init);
// module_exit(scull_exit);
