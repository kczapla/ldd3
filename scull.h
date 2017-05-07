#include <linux/fs.h>

#define SCULL_MAJOR 0
#define SCULL_MINOR 0
#define SCULL_NR_DEVS 4
#define SCULL_QUANTUM 4000
#define SCULL_QSET 1000

dev_t dev;

// scull_fops methods
loff_t scull_llseek(struct file *filp, loff_t f_pos, int count);
int scull_open(struct inode *inode, struct file *filp);
ssize_t scull_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
int scull_trim(struct scull_dev *dev);
ssize_t scull_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos);

// scull's file operation structure
struct file_operations scull_fops = {
    .owner = THIS_MODULE,
    .llseek = scull_llseek,
    .read = scull_read,
    .write = scull_write,
    .ioctl = scill_ioctl,
    .open = scull_open,
    .release = scull_release,
};


struct scull_qset {
    void **data;
    struct scull_qset *next;
}

struct scull_dev {
    struct scull_qset *data;
    int quantum;
    int qset;
    unsigned long size;
    unsigned int access_key;
    struct semaphore sem;
    struct cdev cdev;
};

int get_major_version(void);

int scull_major = SCULL_MAJOR;
int scull_minor = SCULL_MINOR;
int scull_nr_devs = SCULL_NR_DEVS;
int scull_quantum = SCULL_QUANTUM;
int scull_qset = SCULL_QSET;
