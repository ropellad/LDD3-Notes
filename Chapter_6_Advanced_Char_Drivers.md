# LDD3 Chapter 6 Notes

## Chapter 6: Advanced Char Driver Operations

Chapter goal: Understand how to write a fully featured char driver. 

In chaper 3 we built a basic char device from which we could read and write. Real devices have many more features than this, however, and now that we know more about debugging and concurrency we are ready to tackle more advanced operations.

We will start with the ioctl system call, move onto synchronizing with user space, then nonblocking I/O, inform user space of availability for R/W operations, and finally look at access policies. 

### ioctl

In addition to read/write, most drivers need the ability to perform hardware control via the driver. These are things like telling a device to lock its door, eject, and report errors. 

In user space, the ioctl system call has the prototype:

```c
int ioctl(int fd, unsigned long cmd, ...);
```

This prototype is weird for a system call because of the `...` part. Usually system calls do not have variable number of arguments. The `...` in this case indicates that there is a third optional argument. The dots prevent type checking during compilation. The third argument depends on the second argument being used - could be nothing, an INT, or a pointer to other data. 

ioctl call is not very structured, so many kernel developers do not like it. But, ioctl is often the easiest and most straightforward choice for device operations. 

The ioctl driver prototype is slightly different than the user space version:

```c
int (*ioctl) (struct inode *inode, struct file *filp,
    unsigned int cmd, unsigned long arg);
```

- `inode` and `flip` pointers are the values corresponding to the file descriptor `fd` passed by the application. They are the same parameters passed to the `open` method. 
- `cmd` is passed from the user unchanged
- optional `arg` is passed as an unsigned long, regardless of how the user gave it

Most ioctl implementations consist of a big `switch` statement that selects the correct behavior according to the `cmd` argument. Different commands have different numeric values, and are usually given symbolic names in a preprocessor definition to simplify coding. User programs must include the same header file as the driver to have access to the same symbols. 

### Choosing the ioctl Commands

Before writing ioctl code, you must choose numbers that correspond to commands. It might sound nice to start at 0 or 1 and work your way up, but in reality it is good practice to start at a higher, more random number to prevent sending the right command to the wrong device. If each ioctl number is unique, a device will instead give the error `EINVAL` rather than doing something unexpected. 

The methods to choose an ioctl number are defined in `include/asm/ioctl.h` and `Documentation/ioctl-number.txt`. The header defines the bitfields, and the text file lists the magic numbers used in the kernel. The approved way to define ioctl command numbers uses four bitfields which have the following meanings: (see `<linux/ ioctl.h>` for more info)

- Type
  - The magic number. Just choose a new number not in ioctl-number.txt and use it throughout the driver. This field is 8 bits wide. 
- Number
  - The sequential number. It is 8 bits wide. 
- Direction
  - The direction of data transfer. Possible values include:
    - `_IOC_NONE` (no data transfer)
    - `_IOC_READ`
    - `_IOC_WRITE`
    - `_IOC_READ|_IOC_WRITE` (both read and write)
  - This transfer is seen from the application's point of view (read means read from the device)
- Size
  - The size of user data involved. The width of this field is architechure dependent, but usually 13 or 14 bits. You can find it with the macro `_IOC_SIZEBITS`. This field is not mandatory, and the kernel doesn't actually check for it, but it is good practice for debugging. 

The header file `<asm/ioctl.h>` included by `<linux/ioctl.h>` defines macros that help set up command numbers as follows:

- `_IO(type, nr)` for a command with no argument
- `_IOR(type,nr,datatype)` for reading data from the driver
- `_IOW(type,nr,datatype)` for writing data
- `_IOWR(type,nr,datatype)` for bidirectional transfers

`type` and `number` fields are passed as arguments. The `size` field is derived with the `sizeof()` for the datatype.

The header also defines macros that may be used in your driver to decode the numbers:

- `_IOC_DIR(nr)`
- `_IOC_TYPE(nr)`
- `_IOC_NR(nr)`
- `_IOC_SIZE(nr)`

Here is how some ioctl commands are defined in scull:

```c
/* Use 'k' as magic number */
#define SCULL_IOC_MAGIC 'k'
/* Please use a different 8-bit number in your code */
#define SCULL_IOCRESET _IO(SCULL_IOC_MAGIC, 0)
/*
 * S means "Set" through a ptr,
 * T means "Tell" directly with the argument value
 * G means "Get": reply by setting through a pointer
 * Q means "Query": response is on the return value
 * X means "eXchange": switch G and S atomically
 * H means "sHift": switch T and Q atomically
 */
#define SCULL_IOCSQUANTUM _IOW(SCULL_IOC_MAGIC, 1, int)
#define SCULL_IOCSQSET _IOW(SCULL_IOC_MAGIC, 2, int)
#define SCULL_IOCTQUANTUM _IO(SCULL_IOC_MAGIC, 3)
#define SCULL_IOCTQSET _IO(SCULL_IOC_MAGIC, 4)
#define SCULL_IOCGQUANTUM _IOR(SCULL_IOC_MAGIC, 5, int)
#define SCULL_IOCGQSET _IOR(SCULL_IOC_MAGIC, 6, int)
#define SCULL_IOCQQUANTUM _IO(SCULL_IOC_MAGIC, 7)
#define SCULL_IOCQQSET _IO(SCULL_IOC_MAGIC, 8)
#define SCULL_IOCXQUANTUM _IOWR(SCULL_IOC_MAGIC, 9, int)
#define SCULL_IOCXQSET _IOWR(SCULL_IOC_MAGIC,10, int)
#define SCULL_IOCHQUANTUM _IO(SCULL_IOC_MAGIC, 11)
#define SCULL_IOCHQSET _IO(SCULL_IOC_MAGIC, 12)

#define SCULL_IOC_MAXNR 14
```

Integer arguments can be passed as explicit value or (preferred method) pointer. Both ways return an integer number by pointer or by setting the return value. This will work as long as the return value is a positive number (negative is considered an error and is used to set errno in user space). The ordinal number of a command has no meaning. Just use different ones for everything to avoid confusion. The value of the ioctl `cmd` argument is not currently used by the kernel and probably won't be in the future (is it now?). Don't hardcode your numbers using 16-bit scalar values. This is old fashioned and nobody will like it. 

### The Return Value

The implementation of `ioctl` is usually a switch statement based on the command number. But what should the default behavior be (when none of the cases match a value)? You can return `-EINVAL` or the POSIX standard `-ENOTTY`. I will use the POSIX standard. 

### The Predefined Commands

These are separated into three groups:

1. Those that can be issued on any file (regular, device, FIFO, or socket)
2. Those that are issued only on regular files
3. Those specific to the filesystem type

Commands in the third group are executed by the implementation of the hosting filesystem. Device driver writers really only care about the first group of commands, whose magic number is "T". 

The following ioctl commands are predefined for any filem including device-special files:

```
FIOCLEX
    Set the close-on-exec flag (File IOctl CLose on EXec). Setting this flag causes the file descriptor to be closed when the calling process executes a new program.

FIONCLEX
    Clear the close-on-exec flag (File IOctl Not CLos on EXec). The command restores the common file behavior, undoing what FIOCLEX above does.

FIOASYNC
    Set or reset asynchronous notification for the file (as discussed in the section “Asynchronous Notification,” later in this chapter). Note that kernel versions up to Linux 2.2.4 incorrectly used this command to modify the O_SYNC flag. Since both actions can be accomplished through fcntl, nobody actually uses the FIOASYNC command, which is reported here only for completeness.

FIOQSIZE
    This command returns the size of a file or directory; when applied to a device file, however, it yields an ENOTTY error return.

FIONBIO
  “File IOctl Non-Blocking I/O” (described in the section “Blocking and Nonblocking Operations”). This call modifies the O_NONBLOCK flag in filp->f_flags. The third argument to the system call is used to indicate whether the flag is to be set or cleared. (We’ll look at the role of the flag later in this chapter.) Note that the usual way to change this flag is with the fcntl system call, using the F_SETFL command.
```

Note the last item and the mention of `fcntl`. This call is very similar to ioctl but is kept separate for historical reasons. 

### Using the ioctl Argument

This is that mystery third option before that we filled in with `"..."`. For integers it is easy, but pointers are a little more complex. 

When the pointer points to user space, you need to make sure the user address is valid for obvious reasons. Address verification is implemented by the function `access_ok` found in `<asm/uaccess.h>`:

```c
int access_ok(int type, const void *addr, unsigned long size);
```

The first argument should be either `VERIFY_READ` or `VERIFY_WRITE` whether the action being performed is READ-ing the user-space memory area or WRITE-ing it. The `addr` argument holds a user-space address, and `size` is a byte count. 

If you are reading and writing, use `VERIFY_WRITE` since it is a superset of `VERIFY_READ`.

Unlike most kernel functions, access_ok returns a boolean value:

- 1 means success meaning access is okay
- 0 means failure. You should return `-EFAULT` to the caller in this case. 

Interesting things about `access_ok`:

- It does not do the complete job of verifying memory access; it only checks to see that the memory reference is in a region of memory that the process might reasonably have access to. What does this mean??
- It ensures that the address does not point to kernel-space memory
- Most driver code need not actually call access_ok (memory routines take care of it for you)

How scull exploits the bitfields in the ioctl number to check arguments before the switch case is called:

```c
int err = 0, tmp;
int retval = 0;
/*
 * extract the type and number bitfields, and don't decode
 * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok( )
 */
if (_IOC_TYPE(cmd) != SCULL_IOC_MAGIC) return -ENOTTY;
if (_IOC_NR(cmd) > SCULL_IOC_MAXNR) return -ENOTTY;
/*
 * the direction is a bitmask, and VERIFY_WRITE catches R/W
 * transfers. `Type' is user-oriented, while
 * access_ok is kernel-oriented, so the concept of "read" and
 * "write" is reversed
 */
if (_IOC_DIR(cmd) & _IOC_READ)
    err = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
else if (_IOC_DIR(cmd) & _IOC_WRITE)
    err = !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
if (err) return -EFAULT;
```

After calling access_ok, the driver can safely perform the transfer. In addition to copy_from_user and copy_to_user functions, there are more functions optimized for particular data functions defined in `<asm/uaccess.h>`

```
put_user(datum, ptr)
__put_user(datum, ptr)
  These macros write the datum to user space; they are relatively fast and should be called instead of copy_to_user whenever single values are being transferred. The macros have been written to allow the passing of any type of pointer to put_user, as long as it is a user-space address. The size of the data transfer depends on the type of the ptr argument and is determined at compile time using the sizeof and typeof compiler builtins. As a result, if ptr is a char pointer, one byte is transferred, and so on for two, four, and possibly eight bytes. 
  put_user checks to ensure that the process is able to write to the given memory address. It returns 0 on success, and -EFAULT on error. __put_user performs less checking (it does not call access_ok), but can still fail if the memory pointed to is not writable by the user. Thus, __put_user should only be used if the memory region has already been verified with access_ok. 
  As a general rule, you call __put_user to save a few cycles when you are implementing a read method, or when you copy several items and, thus, call access_ok just once before the first data transfer, as shown above for ioctl.

get_user(local, ptr)
__get_user(local, ptr)
  These macros are used to retrieve a single datum from user space. They behave like put_user and __put_user, but transfer data in the opposite direction. The value retrieved is stored in the local variable local; the return value indicates whether the operation succeeded. Again, __get_user should only be used if the address has already been verified with access_ok.
```

If your value does not fit in one of the specific, optimized sizes, you will get a weird message for an error. In that case, just switch back to copy_to_user and copy_from_user.

### Capabilities and Restricted Operations

Access to a device is controlled by the permissions on the device files, and the driver itself is normally not involved with this permissions checking. There are situations where any user has read/write permission on the device, but some control operations should still be denied. Thus, the driver needs to perform some additional checks to make sure the user is capable of performing the requested operation. 

Unix systems traditionally used two users - the normal user and the superuser. The normal user is highly restricted, and the superuser can do anything. Sometimes we need a solution inbetween the two. This is why the kernel provides a more flexible system call capabilities. This system breaks down privileged operations into separate subgroups. The kernel exports two system calls `capget` and `capset` to allow permissions to be managed from user space. The full set of capabilities is in `<linux/capability.h>`. The subset of capabilities were are interested in:

```
CAP_DAC_OVERRIDE
  The ability to override access restrictions (data access control, or DAC) on files and directories.
  
CAP_NET_ADMIN
  The ability to perform network administration tasks, including those that affect network interfaces.
  
CAP_SYS_MODULE
  The ability to load or remove kernel modules.
  
CAP_SYS_RAWIO
  The ability to perform “raw” I/O operations. Examples include accessing device ports or communicating directly with USB devices.
  
CAP_SYS_ADMIN
  A catch-all capability that provides access to many system administration operations.
  
CAP_SYS_TTY_CONFIG
  The ability to perform tty configuration tasks.
```

Before performing a privileged operation, a device driver should check to make sure the calling process has the appropriate capability. Capability checks are performed with the capable function found in `<linux/sched.h>`:

```c
int capable(int capability);
```

In scull, any user can query the quantum and quantum data set sizes. Only privileged users can change those values. Scull implements this with ioctl as follows:

```c
if (! capable (CAP_SYS_ADMIN))
  return -EPERM;
```

### The Implementation of the ioctl Commands

The scull implementation of ioctl only transfers configurable parameters for the device as follows:

```c
switch(cmd) {

  case SCULL_IOCRESET:
    scull_quantum = SCULL_QUANTUM;
    scull_qset = SCULL_QSET;
    break;
    
  case SCULL_IOCSQUANTUM: /* Set: arg points to the value */
    if (! capable (CAP_SYS_ADMIN))
      return -EPERM;
    retval = __get_user(scull_quantum, (int __user *)arg);
    break;
    
  case SCULL_IOCTQUANTUM: /* Tell: arg is the value */
    if (! capable (CAP_SYS_ADMIN))
      return -EPERM;
    scull_quantum = arg;
    break;
    
  case SCULL_IOCGQUANTUM: /* Get: arg is pointer to result */
    retval = __put_user(scull_quantum, (int __user *)arg);
    break;
    
  case SCULL_IOCQQUANTUM: /* Query: return it (it's positive) */
    return scull_quantum;
    
  case SCULL_IOCXQUANTUM: /* eXchange: use arg as pointer */
    if (! capable (CAP_SYS_ADMIN))
      return -EPERM;
    tmp = scull_quantum;
    retval = __get_user(scull_quantum, (int __user *)arg);
    if (retval = = 0)
      retval = __put_user(tmp, (int __user *)arg);
    break;
    
  case SCULL_IOCHQUANTUM: /* sHift: like Tell + Query */
    if (! capable (CAP_SYS_ADMIN))
      return -EPERM;
    tmp = scull_quantum;
    scull_quantum = arg;
    return tmp;
    
  default: /* redundant, as cmd was checked against MAXNR */
    return -ENOTTY;
}
return retval;
```

scull also includes six entries that act on scull_qset. These are identical to the scull_quantum entries. 

From the callers point of view, the six pass and receive arguments look like this:

```c
int quantum;

ioctl(fd,SCULL_IOCSQUANTUM, &quantum); /* Set by pointer */
ioctl(fd,SCULL_IOCTQUANTUM, quantum); /* Set by value */

ioctl(fd,SCULL_IOCGQUANTUM, &quantum); /* Get by pointer */
quantum = ioctl(fd,SCULL_IOCQQUANTUM); /* Get by return value */

ioctl(fd,SCULL_IOCXQUANTUM, &quantum); /* Exchange by pointer */
quantum = ioctl(fd,SCULL_IOCHQUANTUM, quantum); /* Exchange by value */
```

Don't mix a lot of different techniques in a normal driver. Stick to one (all pointers or all values) if possible.

### Device Control Without ioctl

There are times when controlling the device is better by writing control sequences to the device itself. This is used a lot in console drivers like the setterm driver printing escape sequences. The controlling program can live anywhere, and the data stream is simply redirected to the driver. 

The drawback of this strategy is that it add policy constraints to the device. Sometimes control characters can show up in "just text" sections and mess up the behovior of a program (like running cat on a binary file and all the garbage that the terminal spits out). 

Controlling by write is the way to go for devices that don't transfer data but just respond to commands - like a simple robot. When writing command-oriented drivers like this, ioctl makes no sense. Don't implement it. 

You could also do the opposite - get rid of all interpreter write methods, use ioctl exclusively. This approach moves the complexity to user-space and keeps the driver small.

# Go over the differences here ^^^

### Blocking I/O

Sometimes you are not ready to execute a process like read or write because the device is busy. In this case , you want to block the process and put it to sleep until the request can respond. The following section will show how to put a process to sleep and wake it up again later on. But first, a few big ideas:

#### Intro to Sleeping :sleeping: :zzz: :zzz: :zzz:

When a process sleeps, it is marked as being in a special state and removed from the scheduler's run queue. It will not come out of sleep until something comes along to change that state. Putting a process to sleep is easy, but you need to make sure you are sleeping in a safe manner.

Rule 1: Never sleep if you are running in an atomic context. You cannot sleep while holding a spinlock, seqlock, or RCU lock. You also cannot sleep if you have disabled interrupts. 

Rule 2: When you wake up, you never know how long you were out for or what has changed (like a rough Friday night). Make no  assumptions about the state of the system when you wake up. Check every condition you were waiting for is actually true. 

Rule 3: You can't go to sleep unless somebody else is capable of waking you up. This is accomplished with a data structure called a wait queue. It is a list of processes all waiting for a specific event. 

In Linux, a wait queue is managed by means of a wait queue head, which is a structure of type `wait_queue_head_t` defined in `<linux/wait.h>`. It can be allocated statically or dynamically with:

```c
DECLARE_WAIT_QUEUE_HEAD(name); //static init

wait_queue_head_t my_queue; //dynamic init
init_waitqueue_head(&my_queue);
```

### Simple Sleeping

A process must sleep with anticipation that some condition will become true in the future. When woken up, the process needs to check that the condition is actually true! The easiest way to sleep in Linux is a macro call wait_event. It combines handling the details of sleep with a check on the condition. The forms of wait_event:

```c
wait_event(queue, condition)
wait_event_interruptible(queue, condition)
wait_event_timeout(queue, condition, timeout)
wait_event_interruptible_timeout(queue, condition, timeout)
```

- `queue` is the wait queue head to use
- `condition` is an arbitrary boolean condition evaluated by the macro before AND after sleeping
- The interruptible version can be interrupted by signals and is the preferred method to use. This version returns and INT that will be nonzero if a signal interrupted it, and you should return a `-ERESTARTSYS` in that case.
- The final two versions have a timeout measured in jiffies that will only wait for a certain period of time before returning a zero. 

The other half is waking up from sleep. Another thread actually has to wake you up, because you are asleep and won't respond to normal things. The basic function is wake_up and two forms are given below:

```c
void wake_up(wait_queue_head_t *queue);
void wake_up_interruptible(wait_queue_head_t *queue);
```

This will wake up all processes on the current `queue`. Usually, you use the interruptible version for going to sleep with interrupts enabled, since this version will only wake up queue items that have interrupts. 

Sleep is implemented in scull with a module called sleepy:

```c
static DECLARE_WAIT_QUEUE_HEAD(wq);
static int flag = 0;
ssize_t sleepy_read (struct file *filp, char __user *buf, size_t count, loff_t *pos)
{
   printk(KERN_DEBUG "process %i (%s) going to sleep\n",
      current->pid, current->comm);
   wait_event_interruptible(wq, flag != 0);
   flag = 0;
   printk(KERN_DEBUG "awoken %i (%s)\n", current->pid, current->comm);
   return 0; /* EOF */
}
  ssize_t sleepy_write (struct file *filp, const char __user *buf, size_t count,
      loff_t *pos)
{
   printk(KERN_DEBUG "process %i (%s) awakening the readers...\n",
      current->pid, current->comm);
   flag = 1;
   wake_up_interruptible(&wq);
   return count; /* succeed, to avoid retrial */
}
```

Since wait_event_interruptible checks for a condition to be true, the `flag` variable creates that condition. Note there is a possible race condition that opens up two sleeping processes at the same time and they both read the flag as nonzero before they reset it. This operation would need to be done in an atomic matter - we will get to that shortly. 

### Blocking and Nonblocking Operations

When do we put a process to sleep? There are in fact times when you want an operation not to block, even if it cannot be completely carried out. There are also times when the calling process informs you that it does not want to block, whether or not its I/O can make any progress. Explicit nonblocking I/O is indicated by the `O_NONBLOCK` flag in `flip->f_flags`. This flag is defined in `<linux/fcntl.h>` included in `<linux/fs.h>`. The name of `O_NONBLOCK` is derived from "open-nonblock" because it can be specified at open time. How behavior of a blocking operation should be implemented:

- If a process calls read but data is not yet available, the process must block. The process is awakened as soon as data arrives.
- If a process calls write and the buffer has no space, the process must block until room is available on the buffer. This could result in partial writes if `count` is larger than the free space in the buffer.

Almost every driver has input and output buffers. Input prevents loss of data, and output speeds up performance of the system. Having output buffers reduces context switching and user/kernel transistions. The really help speed up performance!

Scull does not use input and output buffers because all data is available and simply copied to the correct location. Chapter 10 will get more into buffers (to get buff).

The behavior of read and write is different if `O_NONBLOCK` is specified. The calls return `-EAGAIN` if a process calls read when no data is available or if it calls write when there's no space left in the buffer. Nonblocking operations return immediately, allowing the application to poll for data. Be careful using stdio while dealing with nonblocking files - do more research on this if you are going to implement it.

`O_NONBLOCK` is important for the open method if you have a long initialization process. It can return `-EAGAIN` after starting the init process to make sure everything is setup properly. 

Overall - only the read, write, and open file operations are affected by the nonblocking flag.

### A Blocking I/O Example

We will look at the scullpipe driver as an implementation of a blocking I/O.

A process blocked in a read call is awakened when data arrives (usually handled as an interrupt). Scullpipe does not use an interrupt handler, however. The device uses two queues and a buffer. The buffer size in configurable in the usual ways.

```c
struct scull_pipe {
   wait_queue_head_t inq, outq; /* read and write queues */
   char *buffer, *end; /* begin of buf, end of buf */
   int buffersize; /* used in pointer arithmetic */
   char *rp, *wp; /* where to read, where to write */
   int nreaders, nwriters; /* number of openings for r/w */
   struct fasync_struct *async_queue; /* asynchronous readers */
   struct semaphore sem; /* mutual exclusion semaphore */
   struct cdev cdev; /* Char device structure */
};
```
 
`read` manages both blocking and nonblocking as the following:

```c
static ssize_t scull_p_read (struct file *filp, char __user *buf, size_t count,
    loff_t *f_pos)
{
    struct scull_pipe *dev = filp->private_data;
    
    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;
 while (dev->rp = = dev->wp) { /* nothing to read */
    up(&dev->sem); /* release the lock */
    
    if (filp->f_flags & O_NONBLOCK)
        return -EAGAIN;
    PDEBUG("\"%s\" reading: going to sleep\n", current->comm);
    
    if (wait_event_interruptible(dev->inq, (dev->rp != dev->wp)))
        return -ERESTARTSYS; /* signal: tell the fs layer to handle it */
        
    /* otherwise loop, but first reacquire the lock */
    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;
    }
    
  /* ok, data is there, return something */
  if (dev->wp > dev->rp)
      count = min(count, (size_t)(dev->wp - dev->rp));
  else /* the write pointer has wrapped, return data up to dev->end */
      count = min(count, (size_t)(dev->end - dev->rp));
  if (copy_to_user(buf, dev->rp, count)) {
      up (&dev->sem);
      return -EFAULT;
  }
  dev->rp += count;
  if (dev->rp = = dev->end)
      dev->rp = dev->buffer; /* wrapped */
  up (&dev->sem);
  /* finally, awake any writers and return */
  wake_up_interruptible(&dev->outq);
  PDEBUG("\"%s\" did read %li bytes\n",current->comm, (long)count);
  return count;
}
```

Some PDEBUG messages are left in there - you can enable/disable them at compile time for the driver with a flag.

Let's look at how scull_p_read handles waiting for the data:

- The while loop tests the buffer with the device semaphore held
- If the data is there, there is no need to sleep and data can be transferred to the user
- If the buffer is empty, we must sleep
- Before sleeping, we NEED to drop the device semaphore
- When the semaphore is dropped, we make a quick check to see if the user has requested non-blocking I/O and return if true. If not, time to call wait_event_interruptible.
- Now we are asleep, and need some way to wake up. One way is if the process receives a signal - the if statement contains a `wait_event_interruptible` call to check for this case. 
- When awoken, you must aquire the device semaphore again (somebody else could have read the data too after waiting like you did!) and check that there is data to be read. 
- Thus, when we exit the while loop, we know the semaphore is held and there is data to be read

### Advanced Sleeping (my favorite grad course)

Let's look first at how a process sleeps:




























