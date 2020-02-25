# LDD3 Chapter 9 Notes

## Chapter 9: Communicating with Hardware

Chapter goal: Understand how a driver can access I/O ports and I/O memory while being portable across Linux platforms. This chapter mainly works with the parallel port found on older machines. It may not apply to current computers as much, but the concepts are still important to understand. 

### I/O Ports and I/O Memory

Every single peripheral device is controlled by writing and reading its registers (wow, that was simple!). Almost all the time a device has many registers which are accessed at consecutive addresses in either the memory space or the I/O address space. 

At the hardware level, memory regions and I/O regions are the same. Both are accessed by asserting electrical signals on the address bus and control bus and by reading from or writing to the data bus. 

Because peripheral devices are built to fit a peripheral bus, the most popular I/O buses are modeled on the personal computer. Linux implements I/O ports on all computer platforms it runs on, even where the CPU implements a single address space. The use of I/O ports is common for ISA peripheral boards, most PCI devices map registers into a memory address region for speed and effiency purposes with the CPU. 

### I/O Registers and Conventional Memory

Someone accessing I/O registers must be careful to avoid being tricked by CPU optimizations that can modify expected I/O behavior. The compiler can cache data into CPU registers without writing them to actual memory. Both write and read operations can operate on cache memory without ever reaching physical RAM! Thus, a driver must ensure that no caching is performed and no read or write reordering takes place when accessing registers.

To fix this issue, we place a memory barrier between operations that need to be visible to hardware in a particular order. There are four macros to do this:

```c
#include <linux/kernel.h>
void barrier(void)
    /*This function tells the compiler to insert a memory barrier but has no effect on
    the hardware. Compiled code stores to memory all values that are currently
    modified and resident in CPU registers, and rereads them later when they are
    needed. A call to barrier prevents compiler optimizations across the barrier but
    leaves the hardware free to do its own reordering. */
    
#include <asm/system.h>
void rmb(void);
void read_barrier_depends(void);
void wmb(void);
void mb(void);
    /*These functions insert hardware memory barriers in the compiled instruction
    flow. An rmb (read memory barrier) guarantees that any reads appearing before the 
    barrier are completed prior to the execution of any subsequent read. wmb 
    guarantees ordering in write operations, and the mb instruction guarantees both. 
    Each of these functions is a superset of barrier. */
    
    /*read_barrier_depends is a special, weaker form of read barrier. rmb prevents the 
    reordering of all reads across the barrier, read_barrier_depends blocks
    only the reordering of reads that depend on data from other reads. You should 
    stick to using rmb. */
    
void smp_rmb(void);
void smp_read_barrier_depends(void);
void smp_wmb(void);
void smp_mb(void);
    /*These insert hardware barriers only when the kernel is compiled for SMP systems. 
    Otherwise, they all expand to a simple barrier call.*/
    
//And a typical use of memory barriers in a device driver may have this general form:
writel(dev->registers.addr, io_destination_address);
writel(dev->registers.size, io_size);
writel(dev->registers.operation, DEV_READ);
wmb( );
writel(dev->registers.control, DEV_GO);
```

Memory buses slow down performance in general, so only use them when you need to. The kernel provides a few useful combinations of variable assignment and memory barrier specification in `<asm/system.h>` as shown:

```c
#define set_mb(var, value) do {var = value; mb( );} while 0
#define set_wmb(var, value) do {var = value; wmb( );} while 0
#define set_rmb(var, value) do {var = value; rmb( );} while 0
```

### Using I/O Ports

#### I/O Port Allocation

You should first get exlusive access to I/O ports before using them. The kernel has a registration interface to allow your driver to claim the port it needs. The core function in that interface is request_region:

```c
#include <linux/ioport.h>

struct resource *request_region(unsigned long first, unsigned long n,
                                const char *name);
```

- Tell the kernel to make use of `n`ports
- Start with the port `first`
- `name` is the name of your device
- Return is non-NULL if allocation succeeds
- If you get a return of NULL, you will not be able to access the ports
- Ports show up in /proc/ioports

When you are done with I/O ports, return them to the system with:

```c
void release_region(unsigned long start, unsigned long n);
```

You can check if a set of I/O ports is available with:

```c
int check_region(unsigned long first, unsigned long n);
```

- This will return negative error code if the port is not available
- This function has been deprecated. Do not use it anymore
- Many old drivers use this code

#### Manipulating I/O Ports

After a driver has locked the use of I/O ports, it must read and/or wrtie to those ports. Most hardware differentiates between 8, 16, and 32 bit systems. Some examples:

- Microchip and the DSPIC line of 8 and 16 bit microcontrollers are very popular
  - They were the "bees knees" of the early 2000s
- The STM32 family by ST Microelectronics is a popular 32 bit platform
  - This is an up and coming platform. ST has recently developed an all-in-one environment for the programming and debugging of these chips. See STMCubeIDE for more info. 
- Arduino Uno is an 8 bit microcontroller based on the ATmega328P chip
  - Better versions have better hardware, but this is just for reference.
- The Teensy 4.0 microcontroller has a ARM Cortex-M7 processor at 600 MHz and is one of the fastest available today (2020). 
  - This thing is REALLY powerful for a microcontroller
- Texas Instruments has everything and does everything well. The only downside is that they are really expensive to use in commercial products. They want the big contracts. ST is more likely to do the smaller scale stuff.  

A c program must call different functions to access different size ports. The Linux kernel headers define the following inline functions to access I/O ports:

```c
unsigned inb(unsigned port);
void outb(unsigned char byte, unsigned port);
    /*Read or write byte ports (eight bits wide). The port argument is defined as
    unsigned long for some platforms and unsigned short for others. The return
    type of inb is also different across architectures.*/
    
unsigned inw(unsigned port);
void outw(unsigned short word, unsigned port);
    /*These functions access 16-bit ports (one word wide); they are not available 
    when compiling for the S390 platform, which supports only byte I/O. */
    
unsigned inl(unsigned port);
void outl(unsigned longword, unsigned port);
    /*These functions access 32-bit ports. longword is declared as either unsigned 
    long or unsigned int, according to the platform. Like word I/O, “long” I/O is 
    not available on S390.*/
```

Note: no 64-bit port I/O operations are defined, even on 64-bit systems. The port address space is at max a 32-bit data path.

#### I/O Port Access from User Space

The functions mentioned just before this are mainly meant to be used by device drivers, but they can also be sued from user space. The GNU C library defines them in `<sys/io.h>`. The following conditions should apply to use them in user space:

- The program must be compiled with the -O option to force expansion of inline functions
- The ioperm or iopl system calls must be used to get permission to perform I/O operations on ports. ioperm gets permission for individual ports, while iopl gets permission for the entire I/O space.
- The program must run as root to invoke ioperm or iopl. Alternatively, one of its ancestors must have gained port access running as root.

#### String Operations

Some processors implement special instructions to transfer a sequence of bytes, words, or longs to and from a single I/O port of the same size. These are called string instructions and they perform a task quicker than C can. The prototypes for string functions are:

```c
void insb(unsigned port, void *addr, unsigned long count);
void outsb(unsigned port, void *addr, unsigned long count);
    /*Read or write count bytes starting at the memory address addr. Data is read 
    from or written to the single port port.*/
    
void insw(unsigned port, void *addr, unsigned long count);
void outsw(unsigned port, void *addr, unsigned long count);
    //Read or write 16-bit values to a single 16-bit port.
    
void insl(unsigned port, void *addr, unsigned long count);
void outsl(unsigned port, void *addr, unsigned long count);
    //Read or write 32-bit values to a single 32-bit port.
```

#### Pausing I/O

Some platforms have problems with timing between the processor and peripheral device. In these situations, a small delay can be inserted after each I/O instruction if another I/O instruction immediately follows. On x86, this is done by performing an `out b` instruction to port 0x80, a normally unused port. Pausing functions are exactly like all of the ones listed above, but they have a `_p` added to the end of them (example: `outb_p` instead of `outb`). 

#### Platform Dependencies

Here are some more relevant details on the platforms that I am most concerned with:

```
x86_64
    The architecture supports all the functions described in this chapter. Port 
    numbers are of type unsigned short.
    
ARM
    Ports are memory-mapped, and all functions are supported. String functions are
    implemented in C. Ports are of type unsigned int.
```

### An I/O Port Example

The sample code we use to show port I/O from within a device driver acts on general-purpose digital I/O ports. These ports are found in most computer systems (are they still?)

Typically, I/O pins are controlled by two I/O locations: one that allows selecting what pins are used as input and what pins are used as output and one in which you can actually read or write logic levels. This makes the GPIO (general purpose input output). Other times, ports can be only read or only write. Then they may only be controlled by one register telling them to do their function. 

### An Overview of the Parallel Port

[Picture of a Parallel Port](https://en.wikipedia.org/wiki/Parallel_port#/media/File:Parallel_computer_printer_port.jpg)

I won't go into a ton of detail on this port because I don't think they are really used at all anymore. But here are some basics:

The parallel interface, in its minimal configuration, is made up of three 8-bit ports. The PC standard starts the I/O ports for the first parallel interface at 0x378 and for the second at 0x278.

- 0x378 is a bidirectional data register
  - It connects directly to pins 2–9 on the physical connector
- 0x278 is a read-only status register
- The third port is an output-only control register which mainly controls whether interrupts are enabled or not
- All ports are on the 0-5V TTL logic level

### Using I/O Memory

The main mechanism used to communicate with devices is memory-mapped registers and device memory. Both of these are called I/O memory because the difference between registers and memory is transparent to software. 

I/O memory is a region of RAM-like locations that the device makes available
to the processor over the bus. It can be used for things like holding video data, holding ethernet packets, and implementing registers that behave like I/O ports. 

This chapter focuses mainly on ISA and PCI memory, but other types should be similar in implementation. PCI is discussed in more detail in chapter 12. 

I/O memory may or may not be accessed through page tables. When access does pass through page tables, the kernel must first arrange for the physical address to be visible from your driver. This usually means that you must call ioremap before doing any I/O operations. If no page tables are needed, I/O memory locations look like I/O ports. You can then just read and write to them using proper wrapper functions.

Whether or not ioremap is required to access I/O memory, direct use of pointers to I/O memory is discouraged. The kernel provides wrapper functions used to access I/O memory that is safe on all platforms and optimized away whenever straight pointer dereferencing can perform the operation.

### I/O Memory Allocation and Mapping

I/O memory regions need to be allocated prior to use. The interface for allocation of memory regions is described in `<linux/ioport.h>` and has prototype:

```c
struct resource *request_mem_region(unsigned long start, unsigned long len,
                                    char *name);
```

- Allocates a memory region of `len` bytes
- Allocates starting at the bit `start`
- On success, a non-NULL pointer is returned. 
- On failure, the return value is NULL.

All I/O memory allocations are listed in /proc/iomem.

Memory regions should be free when no longer needed with:

```c
void release_mem_region(unsigned long start, unsigned long len);
```

There is also an old, now outdated method to check I/O memory region availability with:

```c
int check_mem_region(unsigned long start, unsigned long len);
```

It still shows up sometimes, so it is included here for completeness. 

Next, virtual addresses must be assigned to I/O memory regions with ioremap. Once equipped with ioremap and iounmap, a device driver can access any I/O memory address whether or not it is directly mapped to virtual address space. Here are the prototypes for ioremp:

```c
#include <asm/io.h>

void *ioremap(unsigned long phys_addr, unsigned long size);
void *ioremap_nocache(unsigned long phys_addr, unsigned long size);
void iounmap(void * addr);
```

- The nocache version is often just identical to ioremap. It was meant to be useful if some control registers were in such an area that write combining or read caching was not desirable. This is rare and often not used. 

### Accessing I/O Memory

On some platforms, you may get away with using the return value from ioremap as a
pointer. This is not good though. Instead, a more portable version with functions has been designed as follows:

To read from I/O memory, use one of the following:

```c
unsigned int ioread8(void *addr);
unsigned int ioread16(void *addr);
unsigned int ioread32(void *addr);
```

Where `addr` is an address obtained from ioremap. The return value is what was read from the given I/O memory.

To write to I/O memory, use one of the following:

```c
void iowrite8(u8 value, void *addr);
void iowrite16(u16 value, void *addr);
void iowrite32(u32 value, void *addr);
```

For reading or writing multiple values, use the following:

```c
void ioread8_rep(void *addr, void *buf, unsigned long count);
void ioread16_rep(void *addr, void *buf, unsigned long count);
void ioread32_rep(void *addr, void *buf, unsigned long count);
void iowrite8_rep(void *addr, const void *buf, unsigned long count);
void iowrite16_rep(void *addr, const void *buf, unsigned long count);
void iowrite32_rep(void *addr, const void *buf, unsigned long count);
```

- These functions read or write `count` values from the given `buf` to the given `addr`
- `count` is expressed in the size of the data being written
  - Ex: ioread32_rep reads `count` 32-bit values starting at `buf`

If you need to operate on a block of memory instead, use one of the following:

```c
void memset_io(void *addr, u8 value, unsigned int count);
void memcpy_fromio(void *dest, void *source, unsigned int count);
void memcpy_toio(void *dest, void *source, unsigned int count);
```

- These functions behave like their C library analogs

There are some older read/write functions in legacy kernel code that you should watch out for. Some 64-bit platforms also offer readq and writeq, for quad-word (8-byte) memory operations on the PCI bus.

### Ports as I/O Memory

Some hardware has an interesting feature: some versions use I/O ports, while others
use I/O memory. This seems really confusing to me as to why you would do both. The registers exported to the processor are the same in both cases, but the access method is different (this just sounds like a bad time). To minimize the difference between these two access methods, the kernel provides a function called ioport_map with prototype:

```c
void *ioport_map(unsigned long port, unsigned int count);
```

- It remaps `count` I/O ports and makes them appear to be I/O memory
- From there on the driver can use ioread8 and related functions on the returned addresses
  - This makes it forget that it is using I/O ports at all

The mapping should be undone when no longer needed with:

```c
void ioport_unmap(void *addr);
```

Note: I/O ports must still be allocated with request_region before they can be remapped in this way.

### ISA Memory Below 1 MB

One of the most well-known I/O memory regions is the ISA range found on personal computers. It has a range between 640 KB (0xA0000) and 1 MB (0x100000). This position is due to the fact that in the 1980s 640KB of memory seemed like more than anybody would ever be able to use. Quote Bill Gates:

"640K ought to be enough for anybody" - he denies ever saying this (probably embarrased about it)

Anyways, the ISA memory range belongs to the non-directly-mapped class of memory. To demonstrate access to ISA memory, the driver called silly does the trick. The module supplements the functionality of the module short by giving access to the whole 384-KB memory space and by showing all the different I/O functions.

Because silly provides access to ISA memory, it must first map the physical
ISA addresses into kernel virtual addresses. This is done with ioremap and explained earlier. Code:

```c
#define ISA_BASE 0xA0000
#define ISA_MAX 0x100000 /* for general memory access */

 /* this line appears in silly_init */
 io_base = ioremap(ISA_BASE, ISA_MAX - ISA_BASE);
```
- ioremap returns a pointer value that can be used with ioread8 and the other functions

The following code shows the implementation for read. It makes the address range 0xA0000-0xFFFFF available as a virtual file in the range 0-0x5FFFF. It is structured as a switch case statement for the different access modes. Here is the sillyb case:

```c
case M_8:
    while (count) {
        *ptr = ioread8(add);
        add++;
        count--;
        ptr++;
    }
 break;
```

The next two devices are /dev/sillyw and /dev/sillyl. They act the same as sillyb, but use 16 and 32-bit functions, respectively. Here is the write implementation of sillyl:

```c
case M_32:
    while (count >= 4) {
        iowrite8(*(u32 *)ptr, add);
        add += 4;
        count -= 4;
        ptr += 4;
    }
 break;
```

The last device that is a part of the module is /dev/sillycp. It uses `memcpy_*io` functions to perform the same task.

```c
case M_memcpy:
 memcpy_fromio(ptr, add, count);
 break;
```
Because ioremap was used to provide access to the ISA memory area, silly must
invoke iounmap when the module is unloaded with:

```c
iounmap(io_base);
```

### isa_readb and Friends (again, what is with the Friends? :two_men_holding_hands: )

A look at the kernel source code will show another set of routines with names like isa_readb. Each function just described has an `isa_` equivalent. These functions provide access to ISA memory without the need for a separate ioremap step. Avoid using these! They are temporary band-aids for driver-porting that should be used anymore. It would be sloppy to use these. 