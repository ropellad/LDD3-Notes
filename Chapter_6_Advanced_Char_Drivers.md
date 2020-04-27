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

The drawback of this strategy is that it add policy constraints to the device. Sometimes control characters can show up in "just text" sections and mess up the behavior of a program (like running cat on a binary file and all the garbage that the terminal spits out). 

Controlling by write is the way to go for devices that don't transfer data but just respond to commands - like a simple robot. When writing command-oriented drivers like this, ioctl makes no sense. Don't implement it. 

You could also do the opposite - get rid of all interpreter write methods, use ioctl exclusively. This approach moves the complexity to user-space and keeps the driver small.

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

If you look in `<linux/wait.h>` there will be a data structure for `wait_queue_head_t` that is pretty simple. There is a spinlock and a linked list. The list contains a wait queue entry declared with type `wait_queue_t`. 

The first step in going to sleep is the allocation and initialization of the wait_queue_t structure, followed by its addition to the proper wait queue. That way the person in charge of wakeup can find the right process. 

The next step is to set the task in a state of "asleep". In `<linux/sched.h>` there are several task states defined. Some of them are:

- `TASK_RUNNING` - a process is able to run
- `TASK_INTERRUPTIBLE` - interruptible sleep (use this one)
- `TASK_UNINTERRUPTIBLE` - uninterruptible sleep

You should not directly change the state of a process, but if you need to, use the following:

```c
void set_current_state(int new_state);
```

Now that we have changed our state, we need to formally give up the processor. First, check to make sure that your wakeup condition is not true already, which could create a race condition if you were to check this before setting your state to sleep. Some code like this should exist after setting the process state:

```c
if (!condition)
    schedule( );
```

Also make sure that the state is changed again after waking from sleep!

Manual sleep can still be done. First create a wait queue entry with:

```c
DEFINE_WAIT(my_wait); (preferred)

//OR in 2 steps:
wait_queue_t my_wait;
init_wait(&my_wait);
```

Next, add your wait queue entry to the queue and set the process state. Both are handled with:

```c
void prepare_to_wait(wait_queue_head_t *queue, wait_queue_t *wait, int state);
```

- `queue` and `wait` are the queue head and process entry
- `state` is the new state for the process (usually `TASK_INTERRUPTIBLE`)

After calling this function, the process can call `schedule()` after it has checked that it still needs to wait. Once schedule returns, it is cleanup time. That is handled with:

```c
void finish_wait(wait_queue_head_t *queue, wait_queue_t *wait);
```

Now let's look at the write method for scullpipe:

```c
/* How much space is free? */
static int spacefree(struct scull_pipe *dev)
{
  if (dev->rp = = dev->wp)
    return dev->buffersize - 1;
  return ((dev->rp + dev->buffersize - dev->wp) % dev->buffersize) - 1;
}
static ssize_t scull_p_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
  struct scull_pipe *dev = filp->private_data;
  int result;
  if (down_interruptible(&dev->sem))
    return -ERESTARTSYS;   
 
 /* Make sure there's space to write */
  result = scull_getwritespace(dev, filp);
  if (result)
    return result; /* scull_getwritespace called up(&dev->sem) */
 
 /* ok, space is there, accept something */
  count = min(count, (size_t)spacefree(dev));
  if (dev->wp >= dev->rp)
    count = min(count, (size_t)(dev->end - dev->wp)); /* to end-of-buf */
  else /* the write pointer has wrapped, fill up to rp-1 */
    count = min(count, (size_t)(dev->rp - dev->wp - 1));
 PDEBUG("Going to accept %li bytes to %p from %p\n", (long)count, dev->wp, buf);
 if (copy_from_user(dev->wp, buf, count)) {
  up (&dev->sem);
  return -EFAULT;
 }
 dev->wp += count;
 if (dev->wp = = dev->end)
    dev->wp = dev->buffer; /* wrapped */
 up(&dev->sem);
 
 /* finally, awake any reader */
 wake_up_interruptible(&dev->inq); /* blocked in read( ) and select( ) */
 
 /* and signal asynchronous readers, explained late in chapter 5 */
 if (dev->async_queue)
 kill_fasync(&dev->async_queue, SIGIO, POLL_IN);
 PDEBUG("\"%s\" did write %li bytes\n",current->comm, (long)count);
 return count;
}
```

And the code that handles sleeping is:

```c
/* Wait for space for writing; caller must hold device semaphore. On
 * error the semaphore will be released before returning. */
static int scull_getwritespace(struct scull_pipe *dev, struct file *filp)
{
   while (spacefree(dev) = = 0) { /* full */
      DEFINE_WAIT(wait);
      up(&dev->sem);
      if (filp->f_flags & O_NONBLOCK)
          return -EAGAIN;
      PDEBUG("\"%s\" writing: going to sleep\n",current->comm);
      prepare_to_wait(&dev->outq, &wait, TASK_INTERRUPTIBLE);
      if (spacefree(dev) = = 0)
          schedule( );
      finish_wait(&dev->outq, &wait);
      if (signal_pending(current))
          return -ERESTARTSYS; /* signal: tell the fs layer to handle it */
      if (down_interruptible(&dev->sem))
          return -ERESTARTSYS;
   }
   return 0;
}
```

In this code, if space is available without sleeping, the function just returns. Otherwise, it needs to drop the device semaphore and wait. It uses `DEFINE_WAIT` to set up a wait queue entry and `prepare_to_wait` to get ready for the actual sleep. Then after a quick on the buffer to make sure the wake signal hasn't already been triggered, we can call schedule(). 

In the case where only one process in the queue should awaken instead of all of them, we need a way to handle exclusive waits. It acts like a normal sleep with two big differences:

1. When a wait queue entry has the WQ_FLAG_EXCLUSIVE flag set, it is added to the end of the wait queue. Entries without that flag are, instead, added to the beginning.
2. When wake_up is called on a wait queue, it stops after waking the first process that has the WQ_FLAG_EXCLUSIVE flag set.

The kernel will still wakeup all nonexclusive waiters every time, but with this method an exclusive waiters will not have to create a "thundering herd" of processes waiting their turn still. 

You might use exclusive waits if:

- you expect significant contention for a resource AND
- waking a single process is sufficient to completely consume the resource when it becomes available

This is used for Apache web servers. Putting a process into interruptable wait only requires changing one line:

```c
void prepare_to_wait_exclusive(wait_queue_head_t *queue, wait_queue_t *wait, int state);
```

Now we need to wake up. Here are all the variants to use:

```c
wake_up(wait_queue_head_t *queue);
wake_up_interruptible(wait_queue_head_t *queue);
  /*wake_up awakens every process on the queue that is not in an exclusive wait,
  and exactly one exclusive waiter, if it exists. wake_up_interruptible does the
  same, with the exception that it skips over processes in an uninterruptible sleep.
  These functions can, before returning, cause one or more of the processes awakened 
  to be scheduled (although this does not happen if they are called from an
  atomic context).*/
  
wake_up_nr(wait_queue_head_t *queue, int nr);
wake_up_interruptible_nr(wait_queue_head_t *queue, int nr);
  /*These functions perform similarly to wake_up, except they can awaken up to nr
  exclusive waiters, instead of just one. Note that passing 0 is interpreted as 
  asking for all of the exclusive waiters to be awakened, rather than none of 
  them.*/
  
wake_up_all(wait_queue_head_t *queue);
wake_up_interruptible_all(wait_queue_head_t *queue);
  /*This form of wake_up awakens all processes whether they are performing an
  exclusive wait or not (though the interruptible form still skips processes doing
  uninterruptible waits).*/
  
wake_up_interruptible_sync(wait_queue_head_t *queue);
  /*Normally, a process that is awakened may preempt the current process and be
  scheduled into the processor before wake_up returns. In other words, a call to
  wake_up may not be atomic. If the process calling wake_up is running in an
  atomic context (it holds a spinlock, for example, or is an interrupt handler), 
  this rescheduling does not happen. Normally, that protection is adequate. If, 
  however, you need to explicitly ask to not be scheduled out of the processor at 
  this time, you can use the “sync” variant of wake_up_interruptible. This function 
  is most often used when the caller is about to reschedule anyway, and it is more
  efficient to simply finish what little work remains first.*/
```

Most of the time, drivers just use `wake_up_interruptible` and not any other special variants. 

### Testing the scullpipe Driver

In one terminal, type:

```bash
cat /dev/scullpipe
```

In a different terminal, copy a file to /dev/scullpipe. The data should appear in the first window. 

### Poll and Select

Applications that use nonblocking I/O often use the poll, select, and epoll system calls in combination with the nonblocking. Each of these calls have the same basic functionality: each allow a process to determine whether it can read from or write to one or more open files without blocking. These calls can also block a process until any of a given set of file descriptors becomes available for reading or writing. Therefore, they are often used in applications that must use multiple input or output streams without getting stuck on any one of them. 

Support for these calls comes from the driver's poll method. It has prototype:

```c
unsigned int (*poll) (struct file *filp, poll_table *wait);
```

This method is called whenever poll, select, or epoll are used (they are all the same, just developed concurrently by different groups). The device method must do two things:

1. Call `poll_wait` on one (or more) wait queues that could indicate a change in the poll status. If no file descriptors are currently available for I/O, the kernel causes the process to wait for the wait queues for all file descriptors passed to the system call.
2. Return a bit mask describing the operations that could be immediately performed without blocking.

These operations are simple and similar among many drivers. The `poll_table` structure is used within the kernel to implement the poll, select, and epoll calls (see `<linux/poll.h>`). The driver then adds a wait queue to the poll_table structure by calling poll_wait:

```c
void poll_wait (struct file *, wait_queue_head_t *, poll_table *);
```

The poll method also returns a bit mask describing which operations could be implemented immediately:

```
POLLIN
    This bit must be set if the device can be read without blocking.
    
POLLRDNORM
    This bit must be set if “normal” data is available for reading. A readable 
    device returns (POLLIN | POLLRDNORM).
    
POLLRDBAND
    This bit indicates that out-of-band data is available for reading from the 
    device. It is currently used only in one place in the Linux kernel (the DECnet 
    code) and is not generally applicable to device drivers.
    
POLLPRI
    High-priority data (out-of-band) can be read without blocking. This bit causes
    select to report that an exception condition occurred on the file, because 
    select reports out-of-band data as an exception condition.
    
POLLHUP
    When a process reading this device sees end-of-file, the driver must set POLLHUP
    (hang-up). A process calling select is told that the device is readable, as 
    dictated by the select functionality.
  
POLLERR
    An error condition has occurred on the device. When poll is invoked, the device
    is reported as both readable and writable, since both read and write return an
    error code without blocking.
    
POLLOUT
    This bit is set in the return value if the device can be written to without 
    blocking.
    
POLLWRNORM
    This bit has the same meaning as POLLOUT, and sometimes it actually is the same
    number. A writable device returns (POLLOUT | POLLWRNORM).
    
POLLWRBAND
    Like POLLRDBAND, this bit means that data with nonzero priority can be written 
    to the device. Only the datagram implementation of poll uses this bit, since a 
    datagram can transmit out-of-band data.
```

Here is the scullpipe implementation of poll:

```c
static unsigned int scull_p_poll(struct file *filp, poll_table *wait)
{
     struct scull_pipe *dev = filp->private_data;
     unsigned int mask = 0;
     /*
     * The buffer is circular; it is considered full
     * if "wp" is right behind "rp" and empty if the
     * two are equal.
     */
     down(&dev->sem);
     poll_wait(filp, &dev->inq, wait);
     poll_wait(filp, &dev->outq, wait);
     if (dev->rp != dev->wp)
        mask |= POLLIN | POLLRDNORM; /* readable */
     if (spacefree(dev))
        mask |= POLLOUT | POLLWRNORM; /* writable */
     up(&dev->sem);
     return mask;
}
```

This code adds the two scullpipe wait queues to the poll_table and sets the mask bits depending on whether data can be read or written. Scullpipe does not support an end-of-file condition. 

With real FIFOs the reader sees an end-of-file when all writers close the file. You need to check dev->nwriters in read and in poll to report EOF if no process has the device opened for writing. You also need to make sure to implement blocking within open to avoid a scenario where a reader had opened the file before the writer. 

### Interaction with Read and Write

The purpose of `poll` and `select` calls is to determine in advance if an I/O operation will block. They compliment read and write well, and doing all three at once it descibed below:

#### Reading data from the device:
- If there is data in the input buffer, the read call should return immediately with no big delay.
- You can always return less data than expected. In this case, return `POLLIN|POLLRDNORM`
-  If there is no data in the input buffer, by default read must block until at least one byte is there
-  If `O_NONBLOCK` is set, read returns immediately with a return value of `-EAGAIN`.
-  If we are at end-of-file, read should return immediately with a value of 0. `poll` should report `POLLHUP` in this case. 

#### Writing to the device
- If there is space in the output buffer, write should return without delay
- In this case, `poll` reports the device is writable by returning `POLLOUT|POLLWRNORM`
- If the output buffer is full, by default write blocks until some space is freed
- If `O_NONBLOCK` is set, write returns immediately with a return value of -EAGAIN 
- `poll` should then report the file is not writeable. 
- If the device cannot accept more data, write returns `-ENOSPC`
- Never make a write call wait for data transmission before returning, even if `O_NONBLOCK` is clear

#### Flushing pending output
- The write method alone doesn’t account for all data output needs
- The fsync function/system call fills this gap
- The protoype for fsync is:
  -  `int (*fsync) (struct file *file, struct dentry *dentry, int datasync);`
  -  Waits until device has been completely flushed to return
  -  Not time critical
  -  Char driver commonly has NULL pointer in its `fops`
  -  Block devices always implement the `block-sync` method

### The Underlying Data Structure

The poll_table structure is a wrapper around a function that builds the actual data structure. For poll and select, this is a linked list of memory pages containing poll_table_entry structures. Each poll_table_entry holds the `struct file` and `wait_queue_head_t` pointers passed to poll_wait, along with an associated wait queue entry. A call to poll_wait can also sometimes add the process to the given wait queue. The structure is maintained by the kernel. 

epoll is better when dealing with A LOT of file descriptors to prevent setting up and tearing down the data structure between every I/O operation. It sets up the data structure once and can use it many times. 

### Asynchronous Notification

What if you want to avoid continuous polling? We can implement asynchronous notification so an application can receive a signal whenever data becomes available and not worry about polling. 

Two steps are needed for user programs to enable asynchronous notification:

1. They specify a process as the “owner” of the file. When a process invokes the F_SETOWN command using the fcntl system call, the process ID of the owner process is saved in filp->f_owner for later use.
2. They must set the FASYNC flag in the device by means of the F_SETFL fcntl command.

After these two calls have happened, the input file can request delivery of a `SIGIO` signal whenever new data arrives. The signal is sent to the process in filp->f_owner.

Code example for stdin input file:

```c
signal(SIGIO, &input_handler); /* dummy sample; sigaction( ) is better */
fcntl(STDIN_FILENO, F_SETOWN, getpid( ));
oflags = fcntl(STDIN_FILENO, F_GETFL);
fcntl(STDIN_FILENO, F_SETFL, oflags | FASYNC);
```

Usually only sockets and ttys implement asynchronous notification. If more than one file is enabled to asynchronously notify the process of pending input, the application must still resort to poll or select to find out what happened.

### The Driver's Point of View

How can a device driver implement asynchronous notification? Through the following steps:

1. When F_SETOWN is invoked, nothing happens, except that a value is assigned to filp->f_owner.
2. When F_SETFL is executed to turn on FASYNC, the driver’s fasync method is called. This method is called whenever the value of FASYNC is changed in filp->f_flags to notify the driver of the change, so it can respond properly. The flag is cleared by default when the file is opened.
3. When data arrives, all the processes registered for asynchronous notification are sent a SIGIO signal.

The first step is easy to code from the driver side. The next two require maintaining a dynamic data structure with a lot of help from the kernel. 

We use one data structure and two functions to do all of this. It is all in the header file `<linux/fs.h>`. The data structure is called `struct fasync_struct` and the two functions have the following prototypes:

```c
int fasync_helper(int fd, struct file *filp,
        int mode, struct fasync_struct **fa);
void kill_fasync(struct fasync_struct **fa, int sig, int band);
```

Whew, we have some double pointers in there! Details:

- `fasync_helper` adds or removes entries from the list of interested processes when the FASYNC flag changes for an open file
- `kill_fasync` is used to signal the interested processes when data arrives

How scullpipe implements the fasync method:

```c
static int scull_p_fasync(int fd, struct file *filp, int mode)
{
     struct scull_pipe *dev = filp->private_data;
     return fasync_helper(fd, filp, mode, &dev->async_queue);
}
```

scullpipe does this in the write method as:

```c
if (dev->async_queue)
    kill_fasync(&dev->async_queue, SIGIO, POLL_IN);
```

The last step: invoke the fasync method when the file is closed to remove the file from the list of active asynchronous readers. scullpipe does this in the release method as:

```c
/* remove this filp from the asynchronously notified filp's */
scull_p_fasync(-1, filp, 0);
```

### Seeking a Device with llseek

llseek is useful for some devices and is easy to implement. The method implements the lsddk and llseek system calls. By default, the kernel performs seeks by modifying `filp->f_pos` - the current reading/writing position in the file. 

How scull implements llseek:

```c
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
 if (newpos < 0) return -EINVAL;
 filp->f_pos = newpos;
 return newpos;
}
```

The only device-specific operation in this implementation of llseek is retrieving the file length from the device.

If your device does not support llseek (like a keyboard input stream) you should inform the kernel that your device does not support llseek by calling nonseekable_open in your open method:

```c
int nonseekable_open(struct inode *inode; struct file *filp);
```

This marks the `flip` as nonseekable, and the kernel will prohibit the lseek call on the file. Also make sure to set the llseek method in the `file_operations` structure to the special helper function no_llseek defined in `<linux/fs.h>`.

### Access Control on a Device File

Access control is implemented in the `open` and `release` operations. 

#### Single-Open Devices

The brute force method: permit a device to be opened by only one process at a time. This is called single openness. It is best to avoid this way because of user ingenuity. It may get in the way of what users want to do. This is how scullsingle implents single openness:

```c
static atomic_t scull_s_available = ATOMIC_INIT(1);
static int scull_s_open(struct inode *inode, struct file *filp)
{
     struct scull_dev *dev = &scull_s_device; /* device information */
      if (! atomic_dec_and_test (&scull_s_available)) {
          atomic_inc(&scull_s_available);
          return -EBUSY; /* already open */
      }
     /* then, everything else is copied from the bare scull device */
     if ( (filp->f_flags & O_ACCMODE) = = O_WRONLY)
        scull_trim(dev);
     filp->private_data = dev;
     return 0; /* success */
}
```
The atomic variable scull_s_available decrements with the open call and refuses acess if somebody else already has the device open. 

And the release call marks the device as no longer busy:

```c
static int scull_s_release(struct inode *inode, struct file *filp)
{
 atomic_inc(&scull_s_available); /* release the device */
 return 0;
}
```

### Restricting Access to a Single User at a Time

This is implemented in the open call of sculluid as:

```c
 spin_lock(&scull_u_lock);
 if (scull_u_count &&
       (scull_u_owner != current->uid) && /* allow user */
       (scull_u_owner != current->euid) && /* allow whoever did su */
       !capable(CAP_DAC_OVERRIDE)) { /* still allow root */
    spin_unlock(&scull_u_lock);
    return -EBUSY; /* -EPERM would confuse the user */
 }
 
 if (scull_u_count = = 0)
      scull_u_owner = current->uid; /* grab it */
      
 scull_u_count++;
 spin_unlock(&scull_u_lock);
```

This allows the many processes to work on the device as long as they are from the same owner. A spinlock is implemented to control access to `scull_u_owner` and `scull_u_count`. The corresponding release method then looks like:

```c
static int scull_u_release(struct inode *inode, struct file *filp)
{
   spin_lock(&scull_u_lock);
   scull_u_count--; /* nothing else */
   spin_unlock(&scull_u_lock);
   return 0;
}
```

### Blocking open as an Alternative to EBUSY

Sometimes you want to wait instead of returning an error when a device is busy. This is done by implementing blocking open. The scullwuid driver waits on open instead of returning an error. The only difference is the following:

```c
spin_lock(&scull_w_lock);
while (! scull_w_available( )) {
   spin_unlock(&scull_w_lock);
   if (filp->f_flags & O_NONBLOCK) return -EAGAIN;
   if (wait_event_interruptible (scull_w_wait, scull_w_available( )))
      return -ERESTARTSYS; /* tell the fs layer to handle it */
   spin_lock(&scull_w_lock);
}
if (scull_w_count = = 0)
    scull_w_owner = current->uid; /* grab it */
scull_w_count++;
spin_unlock(&scull_w_lock);
```

It is based on the wait queue. If the device is busy, the process is placed on a wait queue until the owning process closes the device. The release method then needs to awaken any pending process with:

```c
static int scull_w_release(struct inode *inode, struct file *filp)
{
   int temp;
   
   spin_lock(&scull_w_lock);
   scull_w_count--;
   temp = scull_w_count;
   spin_unlock(&scull_w_lock);
   if (temp = = 0)
        wake_up_interruptible_sync(&scull_w_wait); /* awake other uid's */
   return 0;
}
```

### Cloning the Device on Open

One final way to manage access control: create different private copies of a device depending on the process controlling it. This type of access control is rarely needed and is really only done for software devices (hard to clone a physical keyboard, dawg). The scullpriv device node implements virtual devices in the scull package. The book describes the open and release methods for this device, but I am going to forego putting them in these notes because this chapter is already huge and I don't think I will ever need to use this type of access control. 
