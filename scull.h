#include <asm/uaccess.h>
#include <asm-generic/ioctl.h>
#include <linux/fs.h>

#undef PDEBUG
#ifdef SCULL_DEBUG
#  ifdef __KERNEL__
   // Debugging in the kernel space
#    define PDEBUG(fmt, args...) fprintk( KERN_DEBUG "scull: " fmt, ## args)
#  else
   // Debugging in the user space
#    define PDEBUG(fmt, args...) fprintf(stderr, fmt, ## args)
#  endif
#else
#  define pdebug(fmt, args...) //nothing
#endif

// Magic number
#define SCULL_IOC_MAGIC 0xFE
#define SCULL_IOCRESET _IO(SCULL_IOC_MAGIC, 0)
/* 
* S means "Set" through a ptr.
* T means "Tell" directly with the argument value.
* G means "Get": reply by setting through a pointer.
* Q means "Query": response is on the return value.
* X means "eXchange":  Switch G and S atomically.
* H means "sHift": switch T and Q atomically.
*/
#define SCULL_IOCSQUANTUM _IOW(SCULL_IOC_MAGIC, 1, int)
#define SCULL_IOCSQSET    _IOW(SCULL_IOC_MAGIC, 2, int)
#define SCULL_IOCTQUANTUM _IO(SCULL_IOC_MAGIC, 3)
#define SCULL_IOCTQSET    _IO(SCULL_IOC_MAGIC, 4)
#define SCULL_IOCGQUANTUM _IOR(SCULL_IOC_MAGIC, 5, int)
#define SCULL_IOCGQSET    _IOR(SCULL_IOC_MAGIC, 6, int)
#define SCULL_IOCQQUANTUM _IO(SCULL_IOC_MAGIC, 7)
#define SCULL_IOCQQSET    _IO(SCULL_IOC_MAGIC, 8)
#define SCULL_IOCXQUANTUM _IOWR(SCULL_IOC_MAGIC, 9, int)
#define SCULL_IOCXQSET    _IOWR(SCULL_IOC_MAGIC, 10, int)
#define SCULL_IOCHQUANTUM _IO(SCULL_IOC_MAGIC, 11)
#define SCULL_IOCHQSET    _IO(SCULL_IOC_MAGIC, 12)

#define SCULL_IOC_MAXNR 14

#define SCULL_MAJOR 0
#define SCULL_MINOR 0
#define SCULL_NR_DEVS 4
#define SCULL_QUANTUM 4000
#define SCULL_QSET 1000

// scull device number
dev_t dev_no;

// scull's file operation structure forward declaration 
struct file_operations scull_fops;
struct scull_dev;
struct scull_qset;

// scull_fops methods
long scull_unlocked_ioctl(struct file *flip, unsigned int cmd, unsigned long arg);
static int scull_proc_open(struct inode *inode, struct file *file);
static void *scull_seq_start(struct seq_file *s, loff_t *pos);
void scull_seq_stop(struct seq_file *s, void *v);
static void *scull_seq_next(struct seq_file *s, void *v, loff_t *pos);
int scull_seq_show(struct seq_file *s, void *v);
static void scull_cleanup_module(void);
static void scull_exit(void);
struct scull_qset *scull_follow(struct scull_dev *dev, int n);
int scull_open(struct inode *inode, struct file *filp);
ssize_t scull_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
int scull_release(struct inode *inode, struct file *filp);
loff_t scull_llseek(struct file *filp, loff_t off, int whence);
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

//struct scull_dev dev;
struct scull_dev *scull_devs;

extern int scull_major;
extern int scull_minor;
extern int scull_nr_devs;
extern int scull_quantum;
extern int scull_qset;
