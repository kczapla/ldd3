#include <asm/uaccess.h>
#include <linux/fs.h>

#define SCULL_MAJOR 0
#define SCULL_MINOR 0
#define SCULL_NR_DEVS 4
#define SCULL_QUANTUM 4000
#define SCULL_QSET 1000

dev_t dev;

// scull's file operation structure forward declaration 
struct file_operations scull_fops;
struct scull_dev;

// scull_fops methods
int scull_open(struct inode *inode, struct file *filp);
ssize_t scull_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
static void scull_setup_cdev(struct scull_dev *dev, int index);
int scull_trim(struct scull_dev *dev);
ssize_t scull_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos);


struct scull_qset {
    void **data;
    struct scull_qset *next;
};

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

int scull_major;
int scull_minor;
int scull_nr_devs;
int scull_quantum;
int scull_qset;
