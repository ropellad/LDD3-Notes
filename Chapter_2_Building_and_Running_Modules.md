# LDD3 Chapter 2 Notes

## Chapter 2. Building and Running Modules

This chapter introduces all the essential concepts about modules and kernal programming. To avoid an overflow of info all at once, this chapter only talks about modules, without referring to any specific class (char, block, network, etc.). 

### Setting up Your System

I will work these examples with the standard 5.x version of the linux kernel. 

The simplest helloworld program given is:

```
#include <linux/init.h>
#include <linux/module.h>
MODULE_LICENSE("Dual BSD/GPL");
static int hello_init(void)
{
 printk(KERN_ALERT "Hello, world\n");
 return 0;
}
static void hello_exit(void)
{
 printk(KERN_ALERT "Goodbye, cruel world\n");
}
module_init(hello_init);
module_exit(hello_exit);
```

There are two functions defined in this program. One for when the module is is loaded into the kernel, and one for when the module is removed (hello_init and hello_exit, respectively). The module_init and module_exit lines use special kernel macros to indicate the role of these two functions. Another special macro (MODULE_LICENSE) is used to tell the kernel that this module comes with a free license. Without this declaration, the kernel complains when the module is loaded.

`printk()` behaves similarly to the `printf()` function from the C library. The kernel needs its own print function because it runs by itself without any help from the C library. After `insmod` has loaded the program, the module can access the kernel's public symbols. `KERN_ALERT` is used to make the message show up with high priority (otherwise it might not show up anywhere). 

Only the superuser can load and unload a module. This is how the program is supposed to look when running it:

```
% make
make[1]: Entering directory `/usr/src/linux-2.6.10'
 CC [M] /home/ldd3/src/misc-modules/hello.o
 Building modules, stage 2.
 MODPOST
 CC /home/ldd3/src/misc-modules/hello.mod.o
 LD [M] /home/ldd3/src/misc-modules/hello.ko
make[1]: Leaving directory `/usr/src/linux-2.6.10'
% su
root# insmod ./hello.ko
Hello, world
root# rmmod hello
Goodbye cruel world
root#
```
When using a terminal emulator, the messages will likely not show up. Instead, it might be written to a log file such as <i>/var/log/messages</i>.

### Difference Between a Kernel Module and an Application

- While not all applications are event-driven, every kernal module is
- An application can be lazy in how it is destroyed with releaseing resources, while the exit of a module must carefully undo everything from the init() function or the pieces will remain until the system is rebooted
- Faults are handled safely in applications, but they can easily cause a whole system crash in a kernel module
- A module runs in kernel space, where an application runs in user space

Unloading is nice - it allows you to develop without rebooting the system every time you make changes to it. The only functions a kernel module can use are the ones exported by the kernel. There are no libraries to link to.

Due to the lack of libraries, the only includes should come from the kernel source tree, and usual c header files should never be used. 

### Kernal Space versus User Space

Differences:

1. Privilege Level - User space is low, while kernal space has high permission level
2. Each mode has its own memory mapping and address space
3. User space will go into kernel space for system calls and interrupts 

### Concurrency

Most applications fun sequentially, but kernal code must be written with the idea that many things can happen at once. Bad things can happen with other programs, interrupt triggers, running on multiple threads at once, and other issues. As a result, driver code must be <i>reentrant</i>, meaning it is capable of running in more than one context at the same time. Data structures must be carefully designed to keep multiple threads of execution separate, and the code must take care to access shared data in ways that prevent corruption of the data. We must write drivers that avoid concurrency issues as well as race conditions. 

### The Current Process 

Kernel code can refer to the current process by accessing the global item `current`, defined in <asm/current.h>, which gives a pointer to struct task_struct, defined by <linux/sched.h>. The `current` pointer refers to the process that is currently executing. During the execution of a system call (such as open or read) the current process is the one that originally invoked the call. It looks like this:

```
printk(KERN_INFO "The process is \"%s\" (pid %i)\n",
 current->comm, current->pid);
```

`current->comm` is the base name of the program file being executed. 

### A few notes

- Kernels have a very small stack, and you should not declare large automatic variables. Dynamically allocate them at call time instead.
- Functions that start with ( __ ) are low-level and serious. Use with caution
- Kernel code cannot do floating point arithmetic. That is a lot of extra overhead.

### Compiliing Modules

The file <i>Documentation/Changes</i> in the kernel documentation directory always lists the required tool versions to build the kernel. Make sure to obey these!

Make files at first glance are much simpler for kernel modules than for applications. Let's start with the make file for the example:

```
obj-m := hello.o
```

One line. That's it. The resulting file would be `hello.ko` after being built from the object file. With two source files, the make file then becomes:

```
obj-m := module.o
module-objs := file1.o file2.o
```

Running the make command from the command line would then become:
`
make -C ~/kernel-2.6 M=`pwd` modules
`

The -C option changes to the directory of the kernel source code, while `M=` option causes the makefile to move back to the module source directory before trying to build the modules target. 

To simplify the make command from before, we can rework the make file can be changed to:
```
# If KERNELRELEASE is defined, we've been invoked from the
# kernel build system and can use its language.
ifneq ($(KERNELRELEASE),)
 obj-m := hello.o
# Otherwise we were called directly from the command
# line; invoke the kernel build system.
else
 KERNELDIR ?= /lib/modules/$(shell uname -r)/build
 PWD := $(shell pwd)
default:
 $(MAKE) -C $(KERNELDIR) M=$(PWD) modules
endif
```

The make file is read twice on a typical build - but not in the way you might think. The first run sets up the kernel directory, while the second run uses the Makefile contained within the kernel - NOT this make file. Otherwise this loop would go on forever making the same thing over and over again. 

### Loading and Unloading Modules

Once a kernel module is build, the next step is to load it into the kernel. We have previously used <i>insmod</i> to do this. You can also use a number of command line options to assign values to parameters before linking it to the current kernel. This gives the user more flexibility than compile-time configuration. 

In the kernel source, the sames of system calls will always start with sys_. This is useful when grepping!

Another useful utility is <i>modprobe</i>. It works just like insmod, but will check first to make sure that it knows all the kernel symbols being referenced. If there are any it doesn't recognize, it will look in the module search path. Insmod would normally just fail with an unresolved symbols statement.

Linux kernels will often change up formatting and structures, as such it is important to include some form of compatibility in a kernel module. There will need to be some #ifdef operations to get the appropriate build across multiple kernel versions, and there are macros to help with this. Specifically:

`UTS_RELEASE`
This macro expands to a string describing the version of this kernel tree. For example, "2.6.10".

`LINUX_VERSION_CODE`
This macro expands to the binary representation of the kernel version, one byte for each part of the version release number. For example, the code for 2.6.10 is 132618 (i.e., 0x02060a).* With this information, you can (almost)easily determine what version of the kernel you are dealing with.

`KERNEL_VERSION(major,minor,release)`
This is the macro used to build an integer version code from the individual numbers that build up a version number. For example, KERNEL_VERSION(2,6,10) expands to 132618. This macro is very useful when you need to compare the current version and a known checkpoint.

### The Kernel Symbol Table

Insmod resolves undefined symbols against the table of public kernel symbols. When a module is loaded, any symbol exported by the module becomes part of the kernel symbol table. In the usual case, a module implements its own functionality without the need to export any symbols at all. You need to export symbols, however, whenever other modules may benefit from using them.

This concept of module stacking is useful in complex projects. It is implemented in the mainstream kernel (e.g. USB device stacks on usbcore and input modules). This is where modprobe comes in handy - it can automatically load insmod for any dependent modules you need to run to complete the stack. 

The following macros can be used to export symbols:

```
EXPORT_SYMBOL(name);
EXPORT_SYMBOL_GPL(name);
```
The GPL version makes the symbol available to GPL-licensed modules only. This variable is stored in a special part of the module executible (an “ELF section”)that is used by the kernel at load time to find the variables exported by the module.

### Preliminaries

Most kernel code includes a large number of header files. Just about every module has the following headers:

```
#include <linux/module.h>
#include <linux/init.h>
```
`module.h` contains many definitions of symbols and functions needed by loadable modules. You need `init.h` to specify your initialization and cleanup functions. Most modules also include `moduleparam.h` to enable the passing of parameters
to the module at load time.

For good practice, you should also include the liscense of your module.
Example:
```
MODULE_LICENSE("GPL");
```
Different Licenses Recognized:
- GPL - for any version of the GNU General Public License
- GPL v2 - for GPL version two only
- GPL and additional rights
- Dual BSD/GPL
- Dual MPL/GPL
- Proprietary

Unless your module is specifically marked as under a free license, it is assumed proprietary and the kernel is then "tainted." Don't do this. Nobody likes a tainted kernel. 

Other descriptive definitions:
- MODULE_AUTHOR - stating who wrote the module
- MODULE_DESCRIPTION - a human-readable statement of what the module does
- MODULE_VERSION - for a code revision number; see the comments in <linux/module.h> for the conventions to use in creating version strings
- MODULE_ALIAS - another name by which this module can be known
- MODULE_DEVICE_TABLE - to tell user space about which devices the module supports

Module declarations can appear anywhere outside of a function, but recently are being put at the end of the file. 

### Initialization and Shutdown

The module initialization function registers any facility offered by the module. But was does facility mean? A facility is a new functionality, whether it is a whole driver or a new software abstraction that can be accessed by an application. 

The definition of the initialization function always looks like this:

```
static int __init initialization_function(void)
{
 /* Initialization code here */
}
module_init(initialization_function);
```

These need to be `static` because they are not meant to be visible outside of the specific file. This is not a "hard" rule though, because no function is exported to the rest of the kernel unless explicitly requested. It is more of a proper naming convention thing.

The `__init` token in the definition is a hint to the kernel that the function is only used at initialization time. The memory used for the initialization function is dropped after the module is loaded to free up memory for other uses. There is also a `__initdata` token used for data only used during initialization. The use of these two token is optional but worth the extra effort. 

The use of `module_init` is mandatory. This macro adds a special section to the module’s object code stating where the module’s initialization function is to be found. Without this definition your initialization function is never called.

Some facilities the modules can register include devices, filesystems, crypto transforms, and more. For each of these facilities, there is a specific kernel function that accomplishes the registration. The arguments passed to the kernel registration functions are usually pointers to data structures describing the new facility and the name of the facility being registered. This data structure typically contains pointers to module functions. 

### The Cleanup Function

The cleanup function unregisters interfaces and returns all resources to the system before the module is removed entirely. It is usually defined as:

```
static void __exit cleanup_function(void)
{
 /* Cleanup code here */
}
module_exit(cleanup_function);
```

The cleanup value returns void. The `__exit` modifier marks the code as existing for module unload only. This causes the compiler to put it in a special ELF section. A kernel can be configured to disallow the unloading of modules, in which case functions marked with `__exit` are discarded. `module_exit` declaration is necessary to let the kernel find your cleanup function. If there is no cleanup function, the kernel will not allow your module to be unloaded. 

### Error Handling During Initialization

Simple actions can always potentially fail, so module code must check return values to ensure requested operations have completed successfully. If an error does occur, you must go through the following steps:

1. Decide if the module can continue initializing anyway. 
2. If it can continue, it might still be able to operate with degraded functionality if necessary. Always try to continue operation with what IS working when errors happen. 
3. If it cannot continue, you must undo registration activities performed before the failure. If unregistration fails, the kernel is left in an unstable state because it contains pointers that no longer exist.

Error recovery can be handled with the `goto` statement. Normally it is a bad idea to use `goto` in coding, but in this case it is useful. It can eliminate a lot of complicated logic to handle errors. 

The following example code behaves correctly if initialization fails at any point:

```
int __init my_init_function(void)
{
 int err;
 /* registration takes a pointer and a name */
 err = register_this(ptr1, "skull");
 if (err) goto fail_this;
 err = register_that(ptr2, "skull");
 if (err) goto fail_that;
 err = register_those(ptr3, "skull");
 if (err) goto fail_those;
 return 0; /* success */
 fail_those: unregister_that(ptr2, "skull");
 fail_that: unregister_this(ptr1, "skull");
 fail_this: return err; /* propagate the error */
 }
```
Look at the structure of failure. In all three modes:

1. register_this fails: go to the final return error.
2. register_that fails: unregister `_this` first, then return error.
3. register_those fails: unregister `_that`, then unregister `_this`, then final return error.

Otherwise, success is returned if all registrations are successful and the unregistering and return error lines of code are never executed. 

It is customary to unregister everything in reverse order that is was registered. 

#### Error Codes

Returning `err` is used in the previous example. Error codes in the kernel are negative numbers belonging to a set defined in `<linux/errno.h>`. To make your own error codes, include this header file to use symbolic values like -ENODEV, -ENOMEM, and so on. Returning the proper error codes is useful for user programs.

If your initialization and cleanup are more complicated than a few items, the goto approach can be difficult to manage. One other method to minimize code is to call the cleanup function from within the initialization function whenever an error occurs. The cleanup function must check the status of each item before undoing its registration. In a simple form, this looks like:

```
struct something *item1;
struct somethingelse *item2;
int stuff_ok;

void my_cleanup(void)
{
 if (item1)
 release_thing(item1);
 if (item2)
 release_thing2(item2);
 if (stuff_ok)
 unregister_stuff( );
 return;
 }
 
int __init my_init(void)
{
 int err = -ENOMEM;
 item1 = allocate_thing(arguments);
 item2 = allocate_thing2(arguments2);
 if (!item2 || !item2)
 goto fail;
 err = register_stuff(item1, item2);
 if (!err)
  stuff_ok = 1;
 else
 goto fail;
 return 0; /* success */
 fail:
 my_cleanup( );
 return err;
}
```

Note that the cleanup code cannot be marked with `__exit` because it is call by nonexit code.

### Module-Loading Races

Notes on race conditions with initialization:

- Another part of the kernel can make use of any facility you register immediately after that registration has completed. Ensure all internal registration is complete before you register any facility of your code.
- If your initialization function fails, consider what happens if some part of the kernel was already using some facility of your module had already registered. If this happens, consider not failing your module if something useful has already come out of it. If it must fail, keep this in consideration for how to handle that failure. 

### Module Parameters

The kernel supports parameter designations for drivers because hardware can vary so much. The parameter values can be assigned at load time by insmod or modprobe. Modprobe can also read parameters from its config file (etc/modprobe.conf). 

Parameters are declared with the `module_param` macro defined in moduleparam.h. It takes three parameters:

1. The name of the variable
2. The type of the variable
3. A permissions mask to be used for an accompanying sysfs entry

The macro should be placed outside of any function and near the head of a source file. From the very first example, we could add a name input and a number of hello statement input as follows:

```
static char *whom = "world";
static int howmany = 1;
module_param(howmany, int, S_IRUGO);
module_param(whom, charp, S_IRUGO);
```

Variable types that are supported for module parameters:

- bool
- invbool - inverted bool
- charp - char pointer value
- int
- long
- short
- uint - any starting with u are unsigned versions
- ulong
- ushort
- Array parameters - supplied as a comma-separated list 

The array parameters are formatted as follows:

```
module_param_array(name,type,num,perm);
```
- `name` is the name of the array
- `type` is the type of the array elements
- `num` is an integer variable. Usually the number of values supplied
- `perm` is the usual permissions value

All module parameters should be given a default value. `insmod` changes the value only if only if explicitly told by the user.

The `module_param` field is a permission value. Definitions are found in `<linux/stat.h>`. If perm is set to 0, there is no sysfs entry. If it is not 0, it appears under /sys/module with the given set of permissions. 

A few examples:

- `S_IRUGO` can be read by the world but not changed
- `S_IRUGO|S_IWUSR` can be changed by root

It is best practice not to make module parameters writeable unless you know how to detect the change and handle it. 

### Doing it in User Space

Why not write drivers in the user space that are potentially easier and safer to write?

User space drivers provide the following advantages:
1. Full C Library access
2. You can use a conventional debugger
3. If the driver hangs, you can kill it
4. User memory is swappable so it will not take up as much room if infrequently used
5. Can still enable concurrent access to a device
6. Easier to write a closed-source driver (but boo closed source!)

User space drivers usually implement a server process, taking control from the kernel for the task of hardware control. Client applications then connect to the server to perform actual communication with the device. This is how X server works. 

There are also drawbacks to the user space. That is why this book exists. Some of them are:

1. Interrupts are not available
2. Direct memory access is only allowing for a privileged user
3. Access to I/O ports is slower. There are as few ways to do it, but they require privileged access. 
4. Response time is slower
5. If the driver has been swapped to a disk, response time is unacceptably long.
6. The most important devices can't be handled in user space

Bottom line: kernel drivers provide better performance at the cost of having less programming tools to work with.

Quick Reference:

```
insmod

modprobe

rmmod
  User-space utilities that load modules into the running kernels and remove them.

#include <linux/init.h>

module_init(init_function);
module_exit(cleanup_function);
  Macros that designate a module’s initialization and cleanup functions.
  
__init
__initdata
__exit
__exitdata
  Markers for functions (__init and __exit)and data (__initdata and __exitdata) that 
  are only used at module initialization or cleanup time. 
  
#include <linux/sched.h>
  One of the most important header files. This file contains definitions of much of
  the kernel API used by the driver, including functions for sleeping and numerous
  variable declarations.
  
struct task_struct *current;
  The current process.
  
current->pid
current->comm
  The process ID and command name for the current process.

obj-m
  A makefile symbol used by the kernel build system to determine which modules
  should be built in the current directory.
  
/sys/module
/proc/modules
  /sys/module is a sysfs directory hierarchy containing information on currently 
  loaded modules. /proc/modules is the older, single-file version of that 
  information. 

vermagic.o
  An object file from the kernel source directory that describes the environment a
  module was built for.
  
#include <linux/module.h>
  Required header. It must be included by a module source.
  
#include <linux/version.h>
  A header file containing information on the version of the kernel being built.
  
LINUX_VERSION_CODE
  Integer macro, useful to #ifdef version dependencies.
  
EXPORT_SYMBOL (symbol);
EXPORT_SYMBOL_GPL (symbol);
  Macro used to export a symbol to the kernel. The second form exports without
  using versioning information, and the third limits the export to GPL-licensed
  modules.
  
MODULE_AUTHOR(author);
MODULE_DESCRIPTION(description);
MODULE_VERSION(version_string);
MODULE_DEVICE_TABLE(table_info);
MODULE_ALIAS(alternate_name);
  Place documentation on the module in the object file.

module_init(init_function);
module_exit(exit_function);
  Macros that declare a module’s initialization and cleanup functions.
  
#include <linux/moduleparam.h>
module_param(variable, type, perm);
  Macro that creates a module parameter that can be adjusted by the user when
  the module is loaded (or at boot time for built-in code). 

#include <linux/kernel.h>
int printk(const char * fmt, ...);
  The analogue of printf for kernel code.
```
### Bonus: Side Notes

What is an ELF section?

ELF stands for Executable and Linkable Format. It is a standard file format for executable files, object code, shared libraries, and core dumps. 
In Linux, we can use the ELF reader `readelf` to get info about executables. It is useful for telling us about parameter descriptions that we are inputting to the kernel module. It will also tell us things like the symbols being used by the module, and if these symbols come directly from the kernel symbol library. 
