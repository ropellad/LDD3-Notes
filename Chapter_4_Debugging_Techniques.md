# LDD3 Chapter 4 Notes

## Chapter 4: Debugging Techniques

Chapter goal: Learn how to debug kernel code. This is not easy because there is no easy debugger to execute code with, it is not easily traced, and it can be hard to reproduce. 

This chapter introduces ideas to monitor kernel code and trace errors. 

### Debugging Support in the Kernel

Installing your own kernel gives you access to some build-in debugging features. That is one of the reasons we installed our own kernel for this book.

Below are some configuration options for kernel development:

- CONFIG_DEBUG_KERNEL
  - This option makes other debugging options available. It does nothing on its own - it simply enables all the debugging tools.
- CONFIG_DEBUG_SLAB
  - This option turns on several types of checks in the kernel memory allocation functions. With these checks enabled, it is possible to detect many memory overrun and missing initialization errors. Each byte of allocated memory is set to 0xa5 before being handed to the caller and then set to 0x6b when it is freed. If you ever see either of those poison patterns repeating in output from your driver you’ll know exactly what sort of error to look for. When debugging is enabled, the kernel also places special guard values before and after every allocated memory object. If those values ever get changed, the kernel knows that somebody has overrun a memory allocation, and it complains. Checks for more obscure errors are enabled as well.
- CONFIG_DEBUG_PAGEALLOC
  - Full pages are removed from the kernel address space when freed. This option can slow things down, but it can quickly point out certain kinds of memory corruption errors that might otherwise be tricky to find.
- CONFIG_DEBUG_SPINLOCK
  - With this option, the kernel catches operations on uninitialized spinlocks and various other errors.
- CONFIG_DEBUG_SPINLOCK_SLEEP
  - This option enables a check for attempts to sleep while holding a spinlock. It complains if you call a function that could potentially sleep, even if the particular call in question would not sleep.

# REVIEW SPINLOCKS ^^^

- CONFIG_INIT_DEBUG
  - Items marked with __init or __initdata are discarded after system initialization. This option enables checks for code that attempts to access initialization-time memory after initialization is complete.
- CONFIG_DEBUG_INFO
  - This option causes the kernel to be built with full debugging info included. You need that information to debug the kernel with gdb. You might want to enable CONFIG_FRAME_POINTER too if you plan to use gdb.
- CONFIG_MAGIC_SYSRQ
  - Enables the magic "SysRq” key. I have never used this key before, but we look at this key in the section “System Hangs,” later in this chapter.
- CONFIG_DEBUG_STACKOVERFLOW
- CONFIG_DEBUG_STACK_USAGE
  - No, this does not enable the stack overflow forums to directly fix your code for you. These options help track down kernel stack overflows. One sign of a stack overflow is an oops listing without any sort of reasonable back trace. The first option adds explicit overflow checks to the kernel. the second causes the kernel to monitor stack usage and make some stats available via the SysRq key.
- CONFIG_KALLSYMS
  - This option causes kernel symbol information to be built into the kernel. It is enabled by default. The symbol information is used in debugging contexts. without it, an oops listing can give you a kernel traceback only in hexadecimal, which is surely not my language of choice. 
- CONFIG_IKCONFIG
- CONFIG_IKCONFIG_PROC
  - These options cause the full kernel configuration state to be built into the kernel and to be made available via /proc. Most kernel developers know which configuration they used and do not use this. It can be useful, though, if you are trying to debug a problem in a kernel built by somebody else - or a rookie like me who can't remember what I did 10 minutes ago.
- CONFIG_ACPI_DEBUG
  - This is found under “Power management/ACPI.” This option turns on verbose ACPI, Advanced Configuration and Power Interface, debugging information which can be useful if you suspect a problem related to ACPI.
- CONFIG_DEBUG_DRIVER
  - This option is found under “Device drivers.” It turns on debugging information in the driver core, which can be useful for tracking down problems in the low-level support code.
- CONFIG_SCSI_CONSTANTS
  - This option is found under “Device drivers/SCSI device support” and builds in information for verbose SCSI error messages. If you are working on a SCSI driver, you probably want this option.
- CONFIG_INPUT_EVBUG
  - This option is found under “Device drivers/Input device support” and turns on verbose logging of input events. If you are working on a driver for an input device, this option may be useful.
- CONFIG_PROFILING
  - This option is found under “Profiling support.” Profiling is normally used for system performance tuning, but it can also be useful for debugging some kernel hangs and other related problems.

### Debugging by Printing (the lazy, quick, sometimes effective way)

This technique simple calls printk at various points in code to check stuff. Printk lets you classify messages according to their severity. The severity level is called the loglevel. There are eight possible loglevel strings:

1. KERN_EMERG - for emergency messages that precede a crash
2. KERN_ALERT - for situations requiring immediate attention
3. KERN_CRIT - for citical conditions usually related to failures
4. KERN_ERR - for reporting error conditions - for example hardware difficulties 
5. KERN_WARNING - for problematic situations that do not create other problems for the system
6. KERN_NOTICE - for situations that are normal but should have a notice. Usually security related. 
7. KERN_INFO - for informational messages. Like hardware found at boot.
8. KERN_DEBUG - for debugging messages

Each one of these strings represents an integer in angle brackets (0 to 7) to say the priority of the message. Lower number = higher priority. 

Based on the loglevel, the kernel may print the message to the current console or not. If the priority is less than the integer variable `console_loglevel` the message is delivered to the console one line at a time. To see a continuous stream of kernel log output, I have been reading /proc/kmsg using the command `dmesg -wH` to see output from all kernel modules. You can adjust the loglevel default threshold for displaying on the terminal, as well as the default for undefined loglevel in the printk function. These options are found in /proc/sys/kernel/printk.

You can also redirect console messages to different terminals by issuing `ioctl`(TIOCLINUX) on any console device. I won't go into as much detail about that here because dmesg has been working well for me. The complete description of TIOCLINUX can be found in drivers/char/tty_io.c.

### How Messages Get Logged

The printk function writes messages into a circular buffer that is `__LOG_BUF_LEN` bytes long: a value from 4 KB to 1 MB chosen while configuring the kernel. When the buffer fills up, new info from printk simply overwrites the old data. 

If you want to avoid spamming your system log with the monitoring messages from your driver, you can either specify the –f (file) option to klogd to instruct it to save messages to a specific file, or customize /etc/syslog.conf to suit your needs. Another possibility is to take the brute-force approach and kill klogd and then verbosely print messages on an unused virtual terminal, or issue the command cat /proc/kmsg from an unused X terminal. Honestly, dmesg seems to be working fine for now though. 

### Turning the Messages On and Off

The easiest way to do this is to add a flag to your Makefile and implement the flag into your code to turn all the printk statements on or off at once using a custom function (call is PDEBUG if you want, or something similar). You can also add a third option to use fprintf into the user space terminal emulator if you want the messages to appear there too. 

Adding a flag is better than going through and deleting everything with printk statements. The next time you add a feature or fix a bug, you might want all those printk statements back and it would be a pain to reinsert them every time. 

### Rate Limiting Printk

You can use the function `printk_ratelimit()` to reduce the maximum frequency of printed messages. AN example code snippet might look like this:

```
if (printk_ratelimit( ))
 printk(KERN_NOTICE "The printer is still on fire\n");
```

printk_ratelimit works by tracking how many messages are sent to the console. When the level of output exceeds a threshold, printk_ratelimit returns 0 and causes messages to be dropped.

### Printing Device Numbers

The kernel provides a few utility macros defined in `<linux/kdev_t.h>`. They are:

```
int print_dev_t(char *buffer, dev_t dev);
char *format_dev_t(char *buffer, dev_t dev);
```

Both macros encode the device number into the given buffer. The only difference is that `print_dev_t` returns the number of characters printed, while `format_dev_t` returns buffer. `format_dev_t` can be used as a parameter to a printk call directly, although remember that printk doesn’t flush until a trailing newline is provided.

### Debugging by Querying

Many times, the best way to get relevant information is to query the system when you need the information, instead of continually producing data using the printk function. Printk can be very slow as it involves a lot of disk writing operations. Every Unix system provides many tools for obtaining system information: ps, netstat, vmstat, and so on.

A few techniques available to driver developers for querying the system: 
- Creating a file in the /proc filesystem
- Using the ioctl driver method
- Exporting attributes via sysfs

### Using the /proc Filesystem

The /proc filesystem is a special, software-created filesystem the is used by the kernel to export info to the world. Each file in /proc is tied to a kernel function that generates the file's contents on the fly when the file is read. That is why many of these files show up as 0 bytes. They are simply virtual files. 

/proc is used heavily by Linux. Many utilities like ps, top, and uptime get info from /proc. Device drivers can export data to /proc too.

Most /proc entries are read-only, so that is all we will be working with. The reccomended way to info available in new code is via sysfs, but this requires more knowledge of the linux device model. For now, we will write to /proc because it is easier and suitable for debugging purposes. 

### Implementing files in /proc

To work with /proc, include `<linux/proc_fs.h>` to define the right functions. When a process reads from your /proc file, the kernel allocates a page of memory (like a page table in size?) where the driver can write data to be returned to user space. That buffer is passed to a function called read_proc:

```
int (*read_proc)(char *page, char **start, off_t offset, int count, int *eof, void *data);
```

- The page `pointer` is the buffer where you will write your data
- `start` is used to say where interesting data is written in the page
- `offset` and `count` are the same as the read method
- `eof` points to an integer that must be set by the driver to signal that it has no more data to return
- `data` is a driver-specific data pointer you can use for internal organization
- The return value is the number of bytes placed in the page buffer
- Other output files are `*eof` and `*start`. 
  - eof return is simply a flag
  - start specifies where to start reading data. Leaving NULL assumes start at 0. This is fine for small data writes. The book goes into more detail about start and how to start from a different structure in an array of data. 

A better way to implement large /proc files is called seq_file. It will be discussed in more detail in a little bit.

### Creating your /proc file

Once you have a read_proc function defined, you need to connect it to an entry in the /proc hierarchy. This is done with a call to create_proc_read_entry:

```
struct proc_dir_entry *create_proc_read_entry(const char *name,
    mode_t mode, struct proc_dir_entry *base,
    read_proc_t *read_proc, void *data);
```

- `name` is the name of the file to create
- `mode` is the protection mask for the file (0 is system default)
- `base` is the directory in which the file is created (if NULL, goes to /proc root)
- `read_proc` is the read_proc function that implements the file
- `data` is ignored by the kernel and passed to read_proc

The function used by scull to make its /proc function available as /proc/scullmem is:

```
create_proc_read_entry("scullmem", 0 /* default mode */,
    NULL /* parent dir */, scull_read_procmem,
    NULL /* client data */);
```

This creates the scullmem file in /proc with world-readable protections.

Entries in /proc need to be removed when the module is unloaded. remove_proc_entry is the function that undoes when create_proc_read_entry does. Here is the scullmem version:

```
remove_proc_entry("scullmem", NULL /* parent dir */);
```

Two issues with this method: 

1. There is no check if the file is currently in use, so when you close it be sure nothing else is using that file. 
2. There is no checking if a file with the same name already exists. This causes a lot of issues - just avoid it. 

### The seq_file Interface

Ok - back to the seq_file part for writing large /proc files. The interface provides a simple set of functions to implement large files. It creates a virtual file that lets you step through a sequence of items using an iterator.

First, include `<linux/seq_file.h>`. Then, create 4 iterator methods:

1. Start
2. Next
3. Stop
4. Show

The start method is always first:

```
void *start(struct seq_file *sfile, loff_t *pos);
```

- Most of the time, `sfile` can be ignored
- `pos` is the integer position where the reading should start. It could be a byte position or something else.

The start method in scull is:

```
static void *scull_seq_start(struct seq_file *s, loff_t *pos)
{
    if (*pos >= scull_nr_devs)
    return NULL; /* No more to read */
    return scull_devices + *pos;
}
```

The next function needs to move the iterator to the next position, returning NULL if nothing is left in the sequence. Prototype for next:

```
void *next(struct seq_file *sfile, void *v, loff_t *pos);
```

- `v` is the iterator returned from start (or a previous next)
- `pos` is the current position in the file

Here is how scull implements a next method:

```
static void *scull_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
    (*pos)++;
    if (*pos >= scull_nr_devs)
    return NULL;
    return scull_devices + *pos;
}
```

When the kernel is finished using the iterator, it calls the stop method to clean everything up. The prototype for stop is:

```
void stop(struct seq_file *sfile, void *v);
```

Scull doesn't have anything to clean up, so it has no stop method. Between the calls start, next, and stop, the kernel calls the show method to output interesting stuff to user space. The method's prototype is:

```
int show(struct seq_file *sfile, void *v);
```

This method will create output for the item in the sequence indicated by the iterator v. It does not use printk. Instead, there is a special set of functions for seq_file output:

- int seq_printf(struct seq_file *sfile, const char *fmt, ...);
  - This is the printf equivalent for seq_file. It takes the usual format string and additional value arguments. You must also pass it the seq_file structure given to the show function. If seq_printf returns a nonzero value, it means that the buffer is full, and output is being discarded.
- int seq_putc(struct seq_file *sfile, char c);
- int seq_puts(struct seq_file *sfile, const char *s);
  - These behave the same as the user-space putc and puts functions.
- int seq_escape(struct seq_file *m, const char *s, const char *esc);
  - This function is equivalent to seq_puts but any character in s that is also found in esc is printed in octal format. A common value for esc is " \t\n\\", which keeps embedded white space from messing up the output and confusing shell scripts.
- int seq_path(struct seq_file *sfile, struct vfsmount *m, struct dentry *dentry, char *esc);
  - This function is used for outputting the file name associated with a given directory entry. 

The show method implemented in scull is shown below:

```
static int scull_seq_show(struct seq_file *s, void *v)
{
   struct scull_dev *dev = (struct scull_dev *) v;
   struct scull_qset *d;
   int i;
   
   if (down_interruptible(&dev->sem))
      return -ERESTARTSYS;
   seq_printf(s, "\nDevice %i: qset %i, q %i, sz %li\n",
      (int) (dev - scull_devices), dev->qset,
      dev->quantum, dev->size);
   for (d = dev->data; d; d = d->next) { /* scan the list */
      seq_printf(s, " item at %p, qset at %p\n", d, d->data);
      if (d->data && !d->next) /* dump only the last item */
          for (i = 0; i < dev->qset; i++) {
              if (d->data[i])
                  seq_printf(s, " % 4i: %8p\n",i, d->data[i]);
          }
   }
   up(&dev->sem);
   return 0;
}
```

Now that all the methods are defined, scull packages them up and connects them to a file in /proc. First, populate a seq_operations structure:

```
static struct seq_operations scull_seq_ops = {
   .start = scull_seq_start,
   .next = scull_seq_next,
   .stop = scull_seq_stop,
   .show = scull_seq_show
};
```

Instead of using read_proc like before, we will be using a different method that interacts with /proc at a lower level. Create an open method to connect the file to the seq_file operations:

```
static int scull_proc_open(struct inode *inode, struct file *file)
{
    return seq_open(file, &scull_seq_ops);
}
```

Now we are ready to set up a file_operations structure. The only file operation we had to implement ourselves was open (this is nice!). The structure is described below:

```
static struct file_operations scull_proc_ops = {
   .owner = THIS_MODULE,
   .open = scull_proc_open,
   .read = seq_read,
   .llseek = seq_lseek,
   .release = seq_release
};
```

The final step is now to create the actual file in /proc with:

```
entry = create_proc_entry("scullseq", 0, NULL);
if (entry)
  entry->proc_fops = &scull_proc_ops;
```

We used the lower level create_proc_entry instead of create_proc_read_entry like before. The prototype for create_proc_entry is:

```
struct proc_dir_entry *create_proc_entry(const char *name,
  mode_t mode,
  struct proc_dir_entry *parent);
```

Arguments are the same as the `_read` version. 

Why is the seq_file implementation better than the direct /proc version?

- Works no matter the size of the output
- Proper handling of seeks
- Easier to read
- Easier to maintain
- Better for files that contain more than just a few lines of code

### The ioctl Method

Instead of using the /proc filesystem, you can implement a few ioctl commands meant for debugging. These commands can copy data structures from the driver to user space where you can view them.

Benefits of ioctl:

- Runs faster than /proc
- Driver-side code can be simpler to implement than /proc
- Information-retrieval commands can be left in the driver even when debugging would otherwise be disabled. This eliminates weird files in /proc.

Drawbacks of ioctl:

- Need another program to issue to ioctl and display the results
- More difficult to implement
- Module will be slightly bigger in size

### Debugging by Watching

Mainly, use the <i>strace</i> command. It shows all the system calls issued by a user-space program. Useful command line options:

- -t displays time when each call is executed
- -T display time spent in each call
- -e limit the types of calls traced
- -o redirect the output to a file

By default, strace prints info on stderr, which is shown on most X terminal emulators. 

It has a lot of useful info for runtime feedback!

### Debugging System Faults

When kernel code misbahaves, an informative message is usually printed to the console. We will go into how to decode these messages.

#### Oops Messages

Many bugs deal with NULL pointer dereferences or incorrect pointer values. The usual outcome of these bugs is an oops message. This is the SIGSEGV error that you see in code a lot. We have not accessed data in the right way and triggered a page fault. These faults usually show up in the printk output. 

Sample bad output:

```
EIP is at faulty_write+0x4/0x10 [faulty]
```

This is from the function faulty_write. The hex numbers indicate that the instruction pointer was 4 bytes into the function, which appears to be 10 bytes long. Often this much info is sufficient. The call stack listing also provides useful info of how you got to where you are. If you see a bunch of 0xa5a5a5a5 listings, you have very likely forgotten to initialize dynamic memory somewhere. Build your kernel with CONFIG_KALLSYMS turned on to get this symbolic call stack. 

### System Hangs

This happens when a fault never occurs - instead the system seems to be stuck in an infinite loop.

You can prevent an infinite loop by inserting schedule invocations at strategic points. The schedule call invokes the scheduler and allows other processes to steal CPU time/resources from the current process. If a process is looping in kernel space due to a bug in your driver, the schedule calls enable you to kill the process after tracing what is happening. Remember not to call schedule any time your driver is holding a spinlock. 

An great tool to deal with lockups is the magic SysRq key. Magic SysRq is invoked with the combination of the Alt and SysRq keys, and is available on the serial console as well. A third key, pressed simultaneously with these two, performs one of the following actions:

- `r` 
  - Turns off keyboard raw mode. This is useful in situations where a crashed application may have left your keyboard in a strange state.
- `k`
  - Invokes the “secure attention key” (SAK) function. SAK kills all processes running on the current console, leaving you with a clean terminal.
- `s`
  - Performs an emergency synchronization of all disks.
- `u`
  - Unmount. Tries to unmount all disks in a read-only mode. This is usually performed right after `s` to save a lot of filesystem checking if you are in serious trouble.
- `b`
  - Boot. The immediately reboots the system
- `p`
  - Prints processor registers info
- `t`
  - Prints current task list
- `m`
  - Prints memory info

Magic SysRq must be explicitly enabled in the kernel configuration and most distributions do not enable it for security reasons. For a system used to develop drivers, however, enabling magic SysRq is worth the trouble of building a new kernel in itself. Magic SysRq may be disabled at runtime with a command such as:

```
echo 0 > /proc/sys/kernel/sysrq
```

### Debuggers and Related Tools

When all hope is lost, use a debugger to step through the code and watch the value of variables and machine registers. This is time-consuming and should be avoided whenever possible. This means you are having a really bad day.

### Using gdb

gdb is useful for looking as system internals. The debugger must be invoked as though the kernel were an application. In addition to specifying the filename for the ELF kernel image, you need to provide the name of a core file on the command line. For a running kernel, that core file is the kernel core image found in /proc/kcore. To use gdb:

```
gdb /usr/src/linux/vmlinux /proc/kcore
```

- The first argument is the name of the uncompressed ELF kernel executable. 
- The second argument is the name of the core file. 

kcore is used to represent the kernel executable in the format of a core file. Remember /proc files are virtual and call a data generation function rather than a data-retrieval one. Within gdb you can look at kernel variables by issuing the standard gdb commands. When you print data from gdb the kernel is still running, and various data items have different values at different times. gdb optimizes access to the core file by caching data that has already been read so it doesn't get confused all the time.

Notes on gdb:

- gdb cannot edit data - only read it.
- There are no breakpoints or watchpoints
- Kernel must be compiled with CONFIG_DEBUG_INFO option set

### The kdb Kernel Debugger

Why doesn't the kernel have more advanced debuggers built into it? Linus does not believe in interactive debuggers - he thinks they lead to poor fixes. I am in the same boat here - good code does not need a fancy debugger to be understood. 

Some people would disagree, which is why kdb was created as a built-in kernel debugger patch. When kdb is running, almost everything the kernel does stops. Nothing else should be running on a system when you invoke kdb.

#### Updates to kdb and kgdb

Both are now a part of the mainline Linux kernel - no need to install them as patches. The most up-to-date info I can find on kdb is listed here:

- [KDB - eLinux.org](https://elinux.org/KDB)
- [Linux Kernel Debugging \| Dr Dobb's](https://www.drdobbs.com/open-source/linux-kernel-debugging/184406318) 

Kgdb is intended to be used as a source-level debugger to be used in combination with gdb. Two machines are required to use kgdb - one development and one target. More info on Kgdb here:

- [Using kgdb, kdb and the kernel debugger internals](http://landley.net/kdocs/Documentation/DocBook/xhtml-nochunks/kgdb.html)


### The User-Mode Linux Port

This runs on a virtual machine implemented on the Linux system call interface. It allows the kernel to run as a separate, user-mode process on a Linux system. Current info and progress on this can be found here:

- [The User-mode Linux Kernel Home Page](http://user-mode-linux.sourceforge.net/)


### Other Miscellaneous Tools

- The Linux Trace Toolkit (LTT) kernel patch for tracing of events in the kernel. See [LTTng: an open source tracing framework for Linux](https://lttng.org/) for current progress.
- Dynamic Probes (DProbes) - This project looks dead. This page has some useful info: [Dynamic Probes](http://dprobes.sourceforge.net/) 
- This page has a lot of useful debugging info: [Linux kernel live debugging, how it's done and what tools are used? - Stack Overflow](https://stackoverflow.com/questions/4943857/linux-kernel-live-debugging-how-its-done-and-what-tools-are-used) 


Questions:
What is a spinlock? How does it function?

From Wikipedia:
In software engineering, a spinlock is a lock which causes a thread trying to acquire it to simply wait in a loop ("spin") while repeatedly checking if the lock is available. Since the thread remains active but is not performing a useful task, the use of such a lock is a kind of busy waiting.
