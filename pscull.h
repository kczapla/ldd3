#include <asm/uaccess.h>
#include <asm-generic/ioctl.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/wait.h>

#undef PDEBUG
#ifdef SCULL_DEBUG
#  ifdef __KERNEL__
   // Debugging in the kernel space
#    define PDEBUG(fmt, args...) fprintk( KERN_DEBUG "pscull: " fmt, ## args)
#  else
   // Debugging in the user space
#    define PDEBUG(fmt, args...) fprintf(stderr, fmt, ## args)
#  endif
#else
#  define PDEBUG(fmt, args...) //nothing
#endif


#define PSCULL_MAJOR 0
#define PSCULL_MINOR 0
#define PSCULL_NR_DEVS 4
#define PSCULL_BUFFER_SIZE 8000

// pscull device number
dev_t dev_no;

// pscull's file operation structure forward declaration 
struct file_operations pscull_fops;
struct pscull_dev;

// pscull_fops methods
static void pscull_cleanup_module(void);
static void pscull_exit(void);
static int pscull_fasync(int fd, struct file *filp, int mode);
int pscull_open(struct inode *inode, struct file *filp);
static unsigned int pscull_poll(struct file *filp, poll_table *wait);
ssize_t pscull_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
int pscull_release(struct inode *inode, struct file *filp);
static void pscull_setup_cdev(struct pscull_dev *dev, int index);
ssize_t pscull_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos);
static int spacefree(struct pscull_dev *dev);

struct pscull_dev {
	wait_queue_head_t inq, outq;
	char *buffer, *end;
	int buffersize;
	char *rp, *wp;
	int nreaders, nwriters;
	struct fasync_struct *async_queue;
	struct semaphore sem;
	struct cdev cdev;
};

//struct pscull_dev dev;
struct pscull_dev *pscull_devs;

extern int pscull_major;
extern int pscull_minor;
extern int pscull_nr_devs;
