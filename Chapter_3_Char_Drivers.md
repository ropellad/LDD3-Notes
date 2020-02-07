# LDD3 Chapter 3 Notes

## Chapter 3: Char Drivers 

Chapter goal: Write a complete char device driver. (Yay! A real driver!)
This chapter frequently uses <i>scull</i>, which stands for Simple Character Utility for Loading Localities. Scull is a char driver. It acts on a memory area as though it were a device (kind of like a ram disk?). In this chapter, "device" and "the memory area used by scull" mean the same thing. 

Scull is really only useful for demonstration/education purposes. It is like simulating a virtual char device without needing any hardware to do so. 

NOTE: I could not get the source for scull to work with Linux 5.X. I have attached my own char driver at the very end that should compile on the latest current kernel.

### scull Design :skull:

First, we will define the capabilities (see mechanism from Ch.1 notes) the driver will offer to user programs. We can customize scull in many different ways, including several different abstractions on top of computer memory. 

The scull source implements many different devices (listed below). Each kind of device is called a <i>type</i>.

1. scull0, scull1, scull2, scull3
   - These devices have a memory area that is global and persistent. 
      - Global: If the device is opened many times, the data contained in the device is shared by everyone.
      - Persistent: If the device is closed and reopened, data is not lost. 
   - This device can be accessed and tested using conventional commands like cp and cat.
   
   
2. scullpipe0, scullpipe1, scullpipe2, scullpip3
   - Four FIFO devices, which act like pipes. One process reads what another process writes. If multiple processes read the same device they contend for data. Scullpipe will show how blocking and non-blocking <i>read</i> and <i>write</i> can be used without the need for interrupts. While real drivers synchronize their devices with hardware interrupts, blocking and nonblocking operations are still important to learn. 

3. scullsingle, scullpriv, sculluid, scullwuid
   - Like scull0, but with limitations on when <i>open</i> is permitted. scullsingle can only be used by one process at a time. scullpriv is private to each virtual console. scullid and scullwuid can be opened many times, but only by one user. sculluid returns a "Device Busy" error, while scullwuid implements blocking <i>open</i>. Some devices may require this sort of restriction. 

We will be covering scull0-scull3 in this chapter. The remainder of the scull devices are discussed later on in this book in chapter 6. 

### Major and Minor Numbers

A few notes on accessing char devices:
- They are accessed through names in the filesystem
- The names are called special files, device files, or simply nodes of the filesystem tree
- They are typically located in the /dev directory
- Identified by a "c" in the first column of the `ls -l` command (Block devices have a "b" there instead)

Let's look at a sample `ls -l` command from my terminal:
```
crw-rw----  1 root dialout   4,    72 Jan 30 18:26 ttyS8
crw-rw----  1 root dialout   4,    73 Jan 30 18:26 ttyS9
crw-------  1 root root     10,    60 Jan 30 18:26 udmabuf
crw-------  1 root root     10,   239 Jan 30 18:26 uhid
crw-------  1 root root     10,   223 Jan 30 18:26 uinput
crw-rw-rw-  1 root root      1,     9 Jan 30 18:26 urandom
drwxr-xr-x  2 root root            80 Jan 30 18:26 usb
crw-------  1 root root     10,   240 Jan 30 18:26 userio
crw-rw----  1 root tty       7,     0 Jan 30 18:26 vcs
```
The first column has a lot of "c's" in it, meaning those are char devices. The "d" later on means directory. The column containing the 4, 10, 1, and 7 numbers is the major number. The major number identifies the driver associated with the device. The column with 72, 73, 60, etc. is the minor number. This number is used by the kernel to determine which device is being referred to. The kernel does not know much about minor numbers except that they refer to devices implemented by your driver. 

### The Internal Representation of Device Drivers

The `dev_t` type is defined in `<linux/types.h>` and is used to hold device numbers, which includes both the major and minor number parts. Instead of trying to keep track of the internal organization of device numbers (what each bit represents) you should instead use a set of macros found in `<linux/kdev_t.h>`. To get the major and minor numbers, use:

```c
MAJOR(dev_t dev);
MINOR(dev_t dev);
```

You can also go the other way and assign manjor/minor numbers to a `dev_t` type using:

```c
MKDEV(int major, int minor);
```

The format of dev_t has changed since this book was written.

# DISUCSS CHANGES TO dev_t ^^^

### Allocating and Freeing Device Numbers

To set up a char driver, you need to one or more device numbers to work with. The function for this is <i>register_chrdev_region</i>, declared in `<linux/fs.h>`.

Usage:

```c
int register_chrdev_region(dev_t first, unsigned int count, char *name);
```
- `first` is the beginning device number of the range you want to allocate
- The minor number of `first` is often 0, but not required
- `count` is the total number of contiguous device numbers you are requesting
- `name` is the name of the device associated with that number range
- `name` appears in /proc/devices and sysfs
- This function returns 0 if successful, negative if not successful

<i>register_chrdev_region</i> is good if you know exactly which device numbers you want a priori. You can instead use a dynamic allocation method, which requires a different function by the name <i>alloc_chrdev_region</i>. Usage:

```c
int alloc_chrdev_region(dev_t *dev, unsigned int firstminor, unsigned int count, char *name);
```
- `dev` is an output-only parameter that will hold the first number in your allocated range. 
- `firstminor` is the first requested minor number to use (usually 0)
- `count` and `name` parameters work the same as before

Make sure to free device numbers when you no longer need them. They can be freed with the <i>unregister_chrdev_region</i> function, typically used in the module's cleanup function. Usage:

```c
void unregister_chrdev_region(dev_t first, unsigned int count);
```

Before a user-space program can access one of those device numbers, your driver needs to connect them to its internal functions that implement the device's operations. 

### Dynamic Allocation of Major Numbers

A few drivers have major device numbers that are statically assigned, but these are small and not being expanded in the future. It is much safer and better to use dynamic allocation of device numbers to avoid conflicts - especially if you want to distribute your driver.

The disadvantage of using dynamic allocation is that it requires one additional step to implement. Since you can't assign device nodes in advance without knowing the major number, you must read the number from /proc/devices. The process to do this replaces insmod with a simple script that calls insmod and reads /proc/devices to create special files. 

The script <i>scull_load</i> is a shell script to create the devices with the proper major and minor numbers using dynamic allocation. The script can be invoked from the system's <i>rc.local</i> file, or call it manually whenever the module is needed. 

```cmake
#!/bin/sh
module="scull"
device="scull"
mode="664"

# invoke insmod with all arguments we got
# and use a pathname, as newer modutils don't look in . by default
/sbin/insmod ./$module.ko $* || exit 1

# remove stale nodes
rm -f /dev/${device}[0-3]
major=$(awk "\\$2= =\"$module\" {print \\$1}" /proc/devices)
mknod /dev/${device}0 c $major 0
mknod /dev/${device}1 c $major 1
mknod /dev/${device}2 c $major 2
mknod /dev/${device}3 c $major 3

# give appropriate group/permissions, and change the group.
# Not all distributions have staff, some have "wheel" instead.
group="staff"
grep -q '^staff:' /etc/group || group="wheel"
chgrp $group /dev/${device}[0-3]
chmod $mode /dev/${device}[0-3]
```

The group change at the end of the script enables easy switching of user group permissions, depending on how you want your module to work. 

The scull source code also contains an unloading script to clean up the /dev directory and remove the module. I won't be going into too much detail on these files because I have my own char device driver code at the end. 

### Important Data Structures

Most fundamental driver operations involve three important kernel data structures:

1. `file_operations`
2. `file`
3. `inode`

#### 1. File Operations

The file_operations structure is how a char driver sets up the connection between driver operations and device numbers.

The structure is defined in `<linux/fs.h>`, and includes a collection of function pointers. Each open file is associated with its own set of functions. The
operations are primarily in charge of implementing the system calls, and are named open, read, and so on. We can consider the file to be an “object” and the functions operating on it to be its “methods,” using object-oriented programming terminology to denote actions declared by an object to act on itself.

A file_operations structure or a pointer to one is called `fops` or something similar. Each field in the sructure must point to the function in the driver that implements a specific operation or be left NULL for unsupported operations. The behavior of the kernel for a NULL pointer is different for every function, we will list those behaviors shortly. 

Parameters that include the string `__user` note that the pointer is a user-space address that cannot be directly dereferenced. For normal compilation, `__user` has no effect, but it can be used by external checking software for finding the misuse of user-space addresses. 

The following is a REALLY big list of the file operations including their roles, hints, and code examples.

- struct module *owner
  - The first file_operations field is not an operation - it is a pointer to the module that “owns” the structure. This field is used to prevent the module from being unloaded while its operations are in use. Most of the time, it is simply initialized to THIS_MODULE, a macro defined in <linux/module.h>.
- loff_t (*llseek) (struct file *, loff_t, int);
  - The llseek method is used to change the current read/write position in a file, and the new position is returned as a return value. The loff_t parameter is a “long offset” and is at least 64 bits wide. Errors are signaled by a negative return value. If this function pointer is NULL, seek calls will modify the position counter in the file structure in unpredictable ways.
- ssize_t (*read) (struct file *, char __user *, size_t, loff_t *);
  - Used to retrieve data from the device. A null pointer in this position causes the read system call to fail with -EINVAL (“Invalid argument”). A nonnegative return value represents the number of bytes successfully read. The return value is a “signed size” type, usually the default integer type for the target platform.
- ssize_t (*aio_read)(struct kiocb *, char __user *, size_t, loff_t);
  - Initiates an asynchronous read. This is a read operation that might not complete before the function returns. If this method is NULL, all operations will be processed synchronously by read instead.
- ssize_t (*write) (struct file *, const char __user *, size_t, loff_t *);
  - Sends data to the device. If NULL, -EINVAL is returned to the program calling the write system call. If nonnegative, the return value represents the number of bytes successfully written.
- ssize_t (*aio_write)(struct kiocb *, const char __user *, size_t, loff_t *);
  - Starts an asynchronous write on the device.
- int (*readdir) (struct file *, void *, filldir_t);
  - This field should be NULL for device files. It is used for reading directories for filesystems.
- unsigned int (*poll) (struct file *, struct poll_table_struct *);
  - The poll method is the back end of three system calls: poll, epoll, and select, all of which are used to query whether a read or write to one or more file descriptors would block. The poll method should return a bit mask indicating whether nonblocking reads or writes are possible, and provide the kernel with information that can be used to put the calling process to sleep until I/O becomes available again. If a driver leaves its poll method NULL, the device is assumed to be both readable and writable without blocking. This prevents multiple drivers interfering with files at the same time. 
- int (*ioctl) (struct inode *, struct file *, unsigned int, unsigned long);
  - The ioctl system call offers a way to issue device-specific commands like formatting a chunk of a disk. Additionally, a few ioctl commands are recognized by the kernel without referring to the fops table. If the device doesn’t provide an ioctl method, the system call returns an error for any request that isn’t predefined. The return is -ENOTTY, “No such ioctl for device”.
- int (*mmap) (struct file *, struct vm_area_struct *);
  - mmap is used to request a mapping of device memory to a process’s address space. If this method is NULL, the mmap system call returns -ENODEV.
- int (*open) (struct inode *, struct file *);
  - Though this is always the first operation performed on the device file, the driver is not required to declare a corresponding method. If NULL, opening the device always succeeds, but your driver isn’t notified.
- int (*flush) (struct file *);
  - The flush operation is invoked when a process closes its copy of a file descriptor for a device; it should execute (and wait for) any outstanding operations on the device. If flush is NULL, the kernel simply ignores the user application request. That's a nice failure! THIS IS DIFFERENT THAN fsync!
- int (*release) (struct inode *, struct file *);
  - This operation is invoked when the file structure is being released. Like open, release can be NULL.*
- int (*fsync) (struct file *, struct dentry *, int);
  - This method is the back end of the fsync system call, which a user calls to flush any pending data. If this pointer is NULL, the system call returns -EINVAL.
- int (*aio_fsync)(struct kiocb *, int);
  - This is the asynchronous version of the fsync method.
- int (*fasync) (int, struct file *, int);
  - This operation is used to notify the device of a change in its FASYNC flag. Asynchronous notification is an advanced topic and is described later in Chapter 6. The field can be NULL if the driver doesn’t support asynchronous notification.
- int (*lock) (struct file *, int, struct file_lock *);
  - The lock method is used to implement file locking; locking is a super important feature for regular files but is almost never implemented by device drivers (WHY?).
- ssize_t (*readv) (struct file *, const struct iovec *, unsigned long, loff_t *);
- ssize_t (*writev) (struct file *, const struct iovec *, unsigned long, loff_t *);
  - These methods implement scatter/gather read and write operations. Applications occasionally need to do a single read or write operation involving multiple memory areas. These system calls allow them to do so without forcing extra copy operations on the data. If these function pointers are left NULL, the read and write methods are called instead. Nice for reading and writing to multiple places at once.
- ssize_t (*sendfile)(struct file *, loff_t *, size_t, read_actor_t, void *);
  - This method implements the read side of the sendfile system call, which moves the data from one file descriptor to another with a minimum of copying. For example, it is used by a web server that needs to send the contents of a file out a network connection. Device drivers usually leave sendfile NULL.
- ssize_t (*sendpage) (struct file *, struct page *, int, size_t, loff_t *, int);
  - sendpage is the other half of sendfile. It is called by the kernel to send data to the corresponding file. Device drivers do not usually implement sendpage.
- unsigned long (*get_unmapped_area)(struct file *, unsigned long, unsigned long, unsigned long, unsigned long);
  - The purpose of this method is to find a good location in the process’s address space to map in a memory segment on the underlying device. This task is normally performed by the memory management code. This method exists to allow drivers to enforce any alignment requirements a particular device may have. Most drivers can leave this method NULL.
- int (*check_flags)(int)
  - This method enables a module to check the flags passed to a fcntl call.
- int (*dir_notify)(struct file *, unsigned long);
  - This method is invoked when an application uses fcntl to request directory change notifications. It is useful only to filesystems. Drivers should not implement dir_notify.

Scull only implements the most important device methods. The file_operations structrue is thus defined as:

```c
struct file_operations scull_fops = {
   .owner =   THIS_MODULE,
   .llseek =  scull_llseek,
   .read =    scull_read,
   .write =   scull_write,
   .ioctl =   scull_ioctl,
   .open =    scull_open,
   .release = scull_release,
};
```

The file_operations struct uses the standard C tagged structure intialization syntax to make drivers more portable, readable, and compact. 

#### 2. File Structure

The `struct` file defined in `<linux/fs.h>` is the second most important data structure used in device drivers (I would argue all three of these are equally very important). This has nothing to do with FILE pointers in user space programs. Those are a part of the C library - something the kernel does not use. A struct file is different - it is a kernel structure that never appears in user programs. The file structure represents an open file not specific to device drivers. It is created by the kernel when open is called and stays open until all instances are closed and the kernel releases the data structure. 

For our naming conventions: file refers to the structure and filp refers to a pointer to the structure. 

Important fields of struct file are listed below:

- mode_t f_mode;
  - The file mode identifies the file as either readable, writable, or both by means of the bits FMODE_READ and FMODE_WRITE. You might want to check this field for read/write permission in your open or ioctl function. You don’t need to check permissions for read and write - the kernel checks for you before invoking your method. An attempt to read or write when the file has not been opened for that type of access is rejected without the driver knowing about it.
- loff_t f_pos;
  - The current reading or writing position. loff_t is a 64-bit value on all platforms, which is a long long in gcc terminology. The driver can read this value if it needs to know the current position in the file but should not normally change it. Read and write should update a position using the pointer they receive as the last argument instead of acting on filp->f_pos directly. The exception to this rule is the llseek method, the purpose of which is to change the file position.
- unsigned int f_flags;
  - These are the file flags like O_RDONLY, O_NONBLOCK, and O_SYNC. A driver should check the O_NONBLOCK flag to see if nonblocking operation has been requested. The other flags are seldom used. In particular, read/write permission should be checked using f_mode rather than f_flags. All the flags are defined in the header file <linux/fcntl.h>.
- struct file_operations *f_op;
  - The operations associated with the file. The kernel assigns the pointer as part of its implementation of open and then reads it when it needs to dispatch any operations. The value in filp->f_op is never saved by the kernel for later reference. This means that you can change the file operations associated with your file, and the new methods will be effective after you return to the caller. The ability to replace the file operations is the kernel equivalent of method overriding in object-oriented programming.
- void *private_data;
  - The open system call sets this pointer to NULL before calling the open method for the driver. private_data is a useful resource for preserving state information across system calls and is used by most of our sample modules. Remember to release it when finished using it! 
- struct dentry *f_dentry;
  - The directory entry, dentry structure associated with the file. Device driver writers normally do not need to concern themselves with dentry structures, other than to access the inode structure as filp->f_dentry->d_inode.

The real structure has a few fields, but they are not useful to device drivers so we will ignore them. Drivers never create file structures - they only access them. 

#### 3. The inode Structure

The inode structure is used by the kernel to interally represent files. It is different from the file structure that represents an open file descriptor. There can be many file structures representing many open descriptors on a single file, but they all point to a single inode structure.

The inode structure contains a lot of info about a file, but there are only two fields we are interested in for writing device drivers. They are:

- dev_t i_rdev;
  - For inodes that represent device files, this field contains the actual device number.
- struct cdev *i_cdev;
  - struct cdev is the kernel’s internal structure that represents char devices; this field contains a pointer to that structure when the inode refers to a char device file.

To find the major and minor number of an inode, use the following macros:

```c
unsigned int iminor(struct inode *inode);
unsigned int imajor(struct inode *inode);
```

Use these macros instead of changing i_rdev direcctly. 

### Char Device Registration 

The kernel uses structures of type `struct cdev` internally to represent char devices. Before the kernel invokes your device's operations, you must allocate and register one or more of these structures. Include `<linux/cdev.h>` to do this. There are two ways to allocate and initialize one of these structures. If you want a standalone cdev structure at runtime, use:

```c
struct cdev *my_cdev = cdev_alloc( );
my_cdev->ops = &my_fops;
```

If you want to embed the cdev structure within a device-specific structure of your own, intiialize your structure with:

```c
void cdev_init(struct cdev *cdev, struct file_operations *fops);
```

Once the structure is set up, tell the kernel with:

```c
int cdev_add(struct cdev *dev, dev_t num, unsigned int count);
```

- `dev` is the cdev structure
- `num` is the first device number to which this device responds
- `count` is the number of device numbers that should be associated with this device. Most of the time count is 1.

cdev_add can fail, and your device will not be added to the system. If it passes, your device is immediately live and operations can be called by the kernel. Do not call cdev_add until the driver is completely ready to go!

To remove the char device, call:

```c
void cdev_del(struct cdev *dev);
```

Do not access the cdev structure after deleting it. This will cause crashes. 

### Device Registration in scull

Internally, scull represents each device with a structure of type scull_dev. This structure is defined as:

```c
struct scull_dev {
   struct scull_qset *data; /* Pointer to first quantum set */
   int quantum; /* the current quantum size */
   int qset; /* the current array size */
   unsigned long size; /* amount of data stored here */
   unsigned int access_key; /* used by sculluid and scullpriv */
   struct semaphore sem; /* mutual exclusion semaphore */
   struct cdev cdev; /* Char device structure */
};
```

For now we look at cdev. This struct interfaces our device to the kernel. The scull code that handles this task is:

```c
static void scull_setup_cdev(struct scull_dev *dev, int index)
{
   int err, devno = MKDEV(scull_major, scull_minor + index);
   cdev_init(&dev->cdev, &scull_fops);
   dev->cdev.owner = THIS_MODULE;
   dev->cdev.ops = &scull_fops;
   err = cdev_add (&dev->cdev, devno, 1);
   /* Fail gracefully if need be */
   if (err)
   printk(KERN_NOTICE "Error %d adding scull%d", err, index);
}
```
### The `open` Method

The `open` method is provided for a driver to perform initialization in preparation for future operations. `open` should perform the following:

- Check for device-specific errors 
- Initialize the device if it is being opened for the first time
- Update the f_op pointer, if necessary
- Allocate and fill any data structure to be put in filp->private_data

The first thing to do it to identify which device is being opened. The prototype for the open method is:

```c
int (*open)(struct inode *inode, struct file *filp);
```

The inode argument has the information we need from its i_cdev field, which contains the cdev structure we already set up. The only problem is that we do not normally want the cdev structure itself, we want the scull_dev structure that contains the cdev structure. 

Fortunately, there is a macro that takes a pointer to a field of type `containter_field` within a structure of type `container_type` and returns a pointer to the containing structure. The macro is defined in `<linux/kernel.h>` and is shown below:

```c
container_of(pointer, container_type, container_field);
```

In scull_open, this macro is used to find the appropriate device structure:

```c
struct scull_dev *dev; /* device information */

dev = container_of(inode->i_cdev, struct scull_dev, cdev);
filp->private_data = dev; /* for other methods */
```

Once it has found the scull_dev structure, scull stores a pointer to it in the private_data field of the file structure for easier future access.

The other way to identify the device being opened is to look at the minor number stored in the inode structure. If you register your device with register_chrdev, you must use this technique. 

The code for scull_open then becomes:

```c
int scull_open(struct inode *inode, struct file *filp)
{
   struct scull_dev *dev; /* device information */
   dev = container_of(inode->i_cdev, struct scull_dev, cdev);
   filp->private_data = dev; /* for other methods */
   /* now trim to 0 the length of the device if open was write-only */
   if ( (filp->f_flags & O_ACCMODE) = = O_WRONLY) {
   scull_trim(dev); /* ignore errors */
   }
   return 0; /* success */
}
```

### The `release` method

This method is the reverse of open. It needs to do the following:

-  Deallocate anything that open allocated in filp->private_data
-  Shut down the device on last close

Scull has no hardware to shut down, so this method ends up looking like:

```c
int scull_release(struct inode *inode, struct file *filp)
{
   return 0;
}
```

For each open method, there is only one release method. Many close methods can be called for forked processes, but release is only called at the very end when everything is ready to wrap-up. 

### scull's Memory Usage

The scull driver introduces two core functions used to manage memory in the Linux
kernel. These functions are defined in `<linux/slab.h>`:

```c
void *kmalloc(size_t size, int flags);
void kfree(void *ptr);
```

A call to kmalloc attempts to allocate size bytes of memory, the return is a pointer to that data or NULL if it fails. The flags argument is used to describe how the memory should be allocated. For now, use GFP_KERNEL. Allocated memory is free using kfree. You can pass a NULL pointer to kfree. Never use kfree with something your did not kmalloc yourself. 

### `read` and `write`

The read and write methods both copy data from and to application code. Looking back at their prototypes:

```c
ssize_t read(struct file *filp, char __user *buff, size_t count, loff_t *offp);
ssize_t write(struct file *filp, const char __user *buff, size_t count, loff_t *offp);
```

- `flip` is the file pointer
- `count` is the size of the requested data transfer
- `buff` points to the user buffer holding the data to be written or the empty buffer where the newly read data should be placed.
- `offp` is a pointer to a long offset type object and indicated the file position the user is accessing. The return value is a signed size type. 

Remember `buff` is a user-space pointer. It cannot be dereferenced by kernel code. 

Read and write is offered by the following kernel functions:

```c
unsigned long copy_to_user(void __user *to, const void *from, unsigned long count);
unsigned long copy_from_user(void *to, const void __user *from, unsigned long count);
```

They behave like normal memcpy functions. These functions will check if the user space pointer is valid. The return value is the amount of memory still to be copied.The scull code looks for this error return, and returns -EFAULT to the user if it’s not 0. 

Read and write should generally update the file position at the pointer to offp to represent the current file position after successful completion of the system call. The kernel then propagates the file position change back into the file structure when appropriate.

Read and write return negative values on failure. 


### The read Method

Understanding return values:

- The return value of read is the number of bytes transferred. If this is equal to count argument, the transfer was successful.
- If positive, but less than count, an error has happened along the way
- If 0, no data was read
- Negative value is an error (find it in `<linux/errno.h>`)

Here is the code for read in scull:

```c
ssize_t scull_read(struct file *filp, char __user *buf, size_t count,
 loff_t *f_pos)
{
   struct scull_dev *dev = filp->private_data;
   struct scull_qset *dptr; /* the first listitem */
   int quantum = dev->quantum, qset = dev->qset;
   int itemsize = quantum * qset; /* how many bytes in the listitem */
   int item, s_pos, q_pos, rest;
   ssize_t retval = 0;
   
   if (down_interruptible(&dev->sem))
   return -ERESTARTSYS;
   
   if (*f_pos >= dev->size)
      goto out;
   
   if (*f_pos + count > dev->size)
      count = dev->size - *f_pos;
   
   /* find listitem, qset index, and offset in the quantum */
   item = (long)*f_pos / itemsize;
   rest = (long)*f_pos % itemsize;
   s_pos = rest / quantum; q_pos = rest % quantum;
   
   /* follow the list up to the right position (defined elsewhere) */
   dptr = scull_follow(dev, item);
   
   if (dptr = = NULL || !dptr->data || ! dptr->data[s_pos])
      goto out; /* don't fill holes */
      
   /* read only up to the end of this quantum */
   if (count > quantum - q_pos)
      count = quantum - q_pos;
      
   if (copy_to_user(buf, dptr->data[s_pos] + q_pos, count)) {
      retval = -EFAULT;
      goto out;
   }
   
   *f_pos += count;
   retval = count;
   
   out:
      up(&dev->sem);
      return retval;
}
```

### The Write Method

Understanding return values:

- The return value of write is the number of bytes transferred. If this is equal to count argument, the transfer was successful.
- If positive, but less than count, an error has happened along the way
- If 0, nothing was written
- Negative value is an error (find it in `<linux/errno.h>`)

Here is the code for write in scull:\

```c
ssize_t scull_write(struct file *filp, const char __user *buf, size_t count,
 loff_t *f_pos)
{
   struct scull_dev *dev = filp->private_data;
   struct scull_qset *dptr;
   int quantum = dev->quantum, qset = dev->qset;
   int itemsize = quantum * qset;
   int item, s_pos, q_pos, rest;
   ssize_t retval = -ENOMEM; /* value used in "goto out" statements */

   if (down_interruptible(&dev->sem))
      return -ERESTARTSYS;
      
   /* find listitem, qset index and offset in the quantum */
   item = (long)*f_pos / itemsize;
   rest = (long)*f_pos % itemsize;
   s_pos = rest / quantum; q_pos = rest % quantum;
   
   /* follow the list up to the right position */
   dptr = scull_follow(dev, item);
   if (dptr = = NULL)
      goto out;
   if (!dptr->data) {
      dptr->data = kmalloc(qset * sizeof(char *), GFP_KERNEL);
      if (!dptr->data)
        goto out;
      memset(dptr->data, 0, qset * sizeof(char *));
   }
   if (!dptr->data[s_pos]) {
      dptr->data[s_pos] = kmalloc(quantum, GFP_KERNEL);
      if (!dptr->data[s_pos])
        goto out;
   }
   
   /* write only up to the end of this quantum */
   if (count > quantum - q_pos)
      count = quantum - q_pos;
      
   if (copy_from_user(dptr->data[s_pos]+q_pos, buf, count)) {
      retval = -EFAULT;
      goto out;
   }
   
   *f_pos += count;
   retval = count;
   
   /* update the size */
   if (dev->size < *f_pos)
      dev->size = *f_pos;
   out:
     up(&dev->sem);
     return retval;
}
```

### readv and writev

These are the vector versions of the read and write calls. If your driver does not supply methods to handle the vector operations, readv and writev are implemented with multiple calls to your read and write methods. In many situations, however, greater efficiency is acheived by implementing readv and writev directly.

Prototypes for each:

```c
ssize_t (*readv) (struct file *filp, const struct iovec *iov,
  unsigned long count, loff_t *ppos);
ssize_t (*writev) (struct file *filp, const struct iovec *iov,
  unsigned long count, loff_t *ppos);
```

- `flip` and `ppos` arguments are the same as for read and write. 
- The iovec structure defined in `<linux/uio.h>` is:

  ```c
  struct iovec
  {
  void _ _user *iov_base;
  __kernel_size_t iov_len;
  };
  ```

Each iovec describes one chunk of data to be transferred; it starts at iov_base in user space and is iov_len bytes long. The count parameter tells the method how many iovec structures there are. These structures are created by the application, but the kernel copies them into kernel space before calling the driver.

### Linux 5.X Char Driver
Since I could not get scull to work, I went with another online tutorial here: 
[Simple Linux character device driver. – Oleg Kutkov personal blog](https://olegkutkov.me/2018/03/14/simple-linux-character-device-driver/) 

Questions:
What is a quantum and quantum set?
Review inode and ioctl
Can I write something to the char driver, then read the same message it later?

