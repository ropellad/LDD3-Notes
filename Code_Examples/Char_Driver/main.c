// Example Char Driver on Linux
// Originally inspired by Oleg Kutkov <elenbert@gmail.com>

#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/fs.h>

//Set this to whatever you want - it will create more/less virtual devices
#define MAX_DEVICES 4

// Define the functions for open, release, ioctl, read, and write for the char driver
static int     dom_chardev_open   (struct inode *inode, struct file *file);
static int     dom_chardev_release(struct inode *inode, struct file *file);
static long    dom_chardev_ioctl  (struct file *file, unsigned int cmd, unsigned long arg);
static ssize_t dom_chardev_read   (struct file *file, char __user *buf, size_t count, loff_t *offset);
static ssize_t dom_chardev_write  (struct file *file, const char __user *buf, size_t count, loff_t *offset);

// Create and populate a struct for the file operations
static const struct file_operations dom_chardev_fops = {
    .owner          = THIS_MODULE,
    .open           = dom_chardev_open,
    .release        = dom_chardev_release,
    .unlocked_ioctl = dom_chardev_ioctl,
    .read           = dom_chardev_read,
    .write          = dom_chardev_write
};

// This creates a cdev struct
struct dom_char_device_data {
    struct cdev dom_cdev;
};

// Setting dev_major to 0 means dynamically allocate a new major device number for the module - whatever is free
// This is a good practice to avoid conflicts with other modules and drivers
static int dev_major = 0;

// We don't require a class here. Set up as many devices that we wanted earlier with MAX_DEVICES
static struct class *dom_chardev_class = NULL;
static struct dom_char_device_data dom_chardev_data[MAX_DEVICES];

//This is for the uevent. From what I can tell, performing cat uevent gives info on the particular device
static int dom_chardev_uevent(struct device *dev, struct kobj_uevent_env *env)
{
    add_uevent_var(env, "DEVMODE=%#o", 0666);
    return 0;
}

// Initialization function. Only runs at initialization as seen with the __init. This is destroyed when module is loaded
static int __init dom_chardev_init(void)
{
    int err, i; //initialize err and i. err is not used later on. i is the iterator number
    dev_t dev; //define a new dev of the dev_t type

    // Allocate major and minor numbers for each device
    // Start at 0, go up to MAX_DEVICES, name domchardev
    err = alloc_chrdev_region(&dev, 0, MAX_DEVICES, "domchardev");
    
    //Find the major number of the device using the MAJOR() macro
    dev_major = MAJOR(dev);

    // Create the class and add the uevent. Not quite sure what this does. 
    dom_chardev_class = class_create(THIS_MODULE, "domchardev");
    dom_chardev_class->dev_uevent = dom_chardev_uevent;

    // Run a for loop to create MAX_DEVICES number of devices
    for (i = 0; i < MAX_DEVICES; i++) {
        
        // The first option is the cdev from the struct for the particular device. Second option is
        // the file operations struct defined before
        cdev_init(&dom_chardev_data[i].dom_cdev, &dom_chardev_fops);
        
        // Set the owner to be THIS_MODULE on the device
        dom_chardev_data[i].dom_cdev.owner = THIS_MODULE;

        // Add the device to the system. The MKDEV macro makes a device with the major and minor numbers specified
        cdev_add(&dom_chardev_data[i].dom_cdev, MKDEV(dev_major, i), 1);

        // Create a device file node and register with sysfs
        device_create(dom_chardev_class, NULL, MKDEV(dev_major, i), NULL, "domchardev-%d", i);
        
        //print that devices have started
        printk("Successfully started DOMCHARDEV-(%d, %d)\n", dev_major, i);
    }

    return 0;
}

// Meant only for __exit when closing the driver
static void __exit dom_chardev_exit(void)
{
    int i;

    // Destroy the devices and print a message saying so
    for (i = 0; i < MAX_DEVICES; i++) {
        device_destroy(dom_chardev_class, MKDEV(dev_major, i));
        //print that devices have started
        printk("Successfully destroyed DOMCHARDEV-(%d, %d)\n", dev_major, i);
    }

    // Unregister the class, then destroy it
    class_unregister(dom_chardev_class);
    class_destroy(dom_chardev_class);

    unregister_chrdev_region(MKDEV(dev_major, 0), MINORMASK);
}

static int dom_chardev_open(struct inode *inode, struct file *file)
{
    // Simply print that the device has been opened
    printk("DOMCHARDEV-%d: Device open\n", MINOR(file->f_path.dentry->d_inode->i_rdev));
    return 0;
}

static int dom_chardev_release(struct inode *inode, struct file *file)
{
    // Simply print that the device has been closed
    printk("DOMCHARDEV-%d: Device close\n", MINOR(file->f_path.dentry->d_inode->i_rdev));
    return 0;
}

static long dom_chardev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    // ioctl stuff
    printk("DOMCHARDEV-%d: Device ioctl\n", MINOR(file->f_path.dentry->d_inode->i_rdev));
    return 0;
}

// read function for the fops struct
static ssize_t dom_chardev_read(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
    uint8_t *data = "Hello from the kernel world! This is Dom's char device!\n";
    size_t datalen = strlen(data);

    printk("Reading device number: (%d, %d)\n", dev_major, MINOR(file->f_path.dentry->d_inode->i_rdev));

    // Saturate count to data length if needed
    if (count > datalen) {
        count = datalen;
    }

    // If the copy of data fails, return the EFault. The copy_to_user should also give the user the data
    if (copy_to_user(buf, data, count)) {
        return -EFAULT;
    }

    // This should return the number of bytes transferred to the read
    return count;
}

static ssize_t dom_chardev_write(struct file *file, const char __user *buf, size_t count, loff_t *offset)
{
	  //Check my counting logic here. I could be off
    size_t maxdatalen = 10;
    unsigned long ncopied;
    uint8_t databuf[10];
    size_t overflow = 0;
    
    // Use this for debugging count stuff
    //printk("Size of count: %zd\n", count);

    printk("Writing device number: (%d, %d)\n", dev_major, MINOR(file->f_path.dentry->d_inode->i_rdev));

	  //truncate maxdatalen if the bytes to send is less than maxdatalen
    if (maxdatalen > count) {
        maxdatalen = count;
    }
    //Warn if too much data is trying to be written
    else if (maxdatalen < (count)){
    	overflow = count - maxdatalen;
    	printk("Too big of an input. Unable to copy %ld byte(s) from the user\n", (overflow));
    }

    ncopied = copy_from_user(databuf, buf, maxdatalen);
    
    //Print the number of copied bytes from the user
    printk("Copied %zd bytes from the user\n", maxdatalen);
    databuf[maxdatalen] = 0;

	  //Print the copied bytes from the user
    printk("Data from the user: %s\n", databuf);

    return count;
}

module_init(dom_chardev_init);
module_exit(dom_chardev_exit);

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("Dom");
