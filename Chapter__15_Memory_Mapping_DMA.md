# LDD3 Chapter 15 Notes

## Chapter 15: Memory Mapping and DMA

Chapter Goal: Learn in-depth about three things: mmap, access to user-space pages, and DMA I/O operations. 

### Memory Management in Linux

You should be familiar with the concept of virtual addresses. Actual user-space programs can allocate more memory than a machine has, and every program has its own virtual address space that it accesses. The following is a more detailed list of all the address types in Linux:

- User virtual addresses
  - These are the regular addresses seen by user-space programs
  - Either 32 or 64 bits in length depending on architechure
  - Each process has its own virtual address space
- Physical addresses
  - The addresses used between the processor and the system’s memory
  - 32- or 64-bit quantities
- Bus addresses
  - The addresses used between peripheral buses and memory
  - Often the same as the physical addresses used by the processor
  - Some architectures can provide an I/O memory management unit (IOMMU) that remaps addresses between a bus and main memory
- Kernel logical addresses
  - These make up the normal address space of the kernel
  - Map some portion of main memory and are often treated as if they were physical addresses
  - Probably only differs from physical address by a constant offset
  - Logical addresses use the hardware’s native pointer size
  - Stored in variables of type unsigned long or void *
  - This is the return address from kmalloc
- Kernel virtual addresses
  - Similar to logical addresses in that they are a mapping from a kernel-space address to a physical address
  - Do NOT necessarily have the linear, one-to-one mapping to physical addresses that characterize the logical address space
  - All logical addresses are kernel virtual addresses
    - But many kernel virtual addresses are NOT logical addresses

### Physical Addresses and Pages

Most systems have a page size of 4096 bytes. This is defined with PAGE_SIZE. You should be using PAGE_SIZE in your code instead of a hard-coded 4096. With this page size, the 12 lesat-significant bits are the offset, and the remaining bits indicate the page number. If you discard the offset and shift the rest of the bits to the right, the result is called the page frame number (PFN). Doing this is common, and the macro PAGE_SHIFT tells how many bits must be shifted to make this conversion. 

### High and Low Memory

The kernel needs its own virtual address space separate from the rest of the user-space programs. Many kernel data structures must be placed in low memory, while high memory tends to be reserved for user-space process pages. Here is a concrete difference between high and low memory:

- Low Memory
  - Memory for which logical addresses exist in kernel space
  - On almost every system you will likely encounter, all memory is low memory
- High memory
  - Memory for which logical addresses do not exist because it is beyond the address range set aside for kernel virtual addresses

### The Memory Map and Struct Page

For dealing with pages of memory, both logical and high memory, the kernel uses the `struct page` structure found in `<linux/mm.h>`. There is one of these structures for every physical page on the system. The important fields of the struct are:

- `atomic_t count;`
  - The number of references there are to this page
  - When the count drops to 0, the page is returned to the free list
- `void *virtual;`
  - The kernel virtual address of the page if it is mapped
    - NULL otherwise
  - Low memory pages are always mapped
  - High memory pages are usually not mapped
  - Does not appear on all architectures
- `unsigned long flags;`
  - A set of bit flags describing the status of the page
  - `PG_locked` indicates that the page has been locked in memory
  - `PG_reserved` prevents the memory management system from working with the page at all

The kernel keeps one or more arrays of struct page entries that track all of the physical memory on the system. On some systems, there is a single array called mem_map. Other systems have nonuniform memory access (NUMA) and have more than one memory map array. For the most part it is east to work with struct page pointers without worrying about where they come from. Here are some functions for translating between struct page pointers and virtual addresses:

- `struct page *virt_to_page(void *kaddr);`
  - This macro takes a kernel logical address and returns its associated struct page pointer
  - It requires a logical address and does not work with memory from vmalloc or high memory
- `struct page *pfn_to_page(int pfn);`
  - Returns the struct page pointer for the given page frame number
- `void *page_address(struct page *page);`
  - Returns the kernel virtual address of this page, if such an address exists
  - For high memory, that address exists only if the page has been mapped
  - In most situations, you want to use a version of kmap rather than page_address
- `#include <linux/highmem.h>` AND
- `void *kmap(struct page *page);` AND
- `void kunmap(struct page *page);`
  - kmap returns a kernel virtual address for any page in the system
    - For low-memory pages, it just returns the logical address of the page
    - For high-memory pages, kmap creates a special mapping in a dedicated part of the kernel address space
    - kmap can sleep if no mappings are available
  - Mappings created with kmap should always be freed with kunmap
- `#include <linux/highmem.h>` AND
- `#include <asm/kmap_types.h>` AND
- `void *kmap_atomic(struct page *page, enum km_type type);` AND
- `void kunmap_atomic(void *addr, enum km_type type);`
  - kmap_atomic is a high-performance form of kmap
  - It is a smaller list, and you have less of them to use!
  - The kernel does not prevent two functions from trying to use the same slot - be careful when using these

### Page Tables  :page_facing_up: 

Page tables convert virtual addresses into corresponding physical addresses. It is a multilevel tree-structured array containing virtual-to-physical mappings and a few associated flags. We no longer need to work with page tables directly, so we will not go into them in this noteset. 

### Virtual Memory Areas

The virtual memory area (VMA) is the kernel data structure used to manage distinct regions of a process’s address space. A VMA is a continuous region in the virtual memory of a process. All addresses in the range have the same permission flags and are backed up by the same object. It is kind of like a segment, but is better described by a memory object with its own properties. The memory map of every process has at least the following areas:

- An area for the program’s executable code (often called text)
- Multiple areas for data, including initialized data, uninitialized data (BSS), and the program stack
- One area for each active memory mapping

The memory areas of a process can be viewed by looking in /proc/pid/maps. Here is one example:

```shell
$ sudo cat /proc/1321/maps 
556c90eb3000-556c90ebc000 r-xp 00000000 103:03 23335669                  /usr/lib/gnome-settings-daemon/gsd-rfkill
556c910bb000-556c910bc000 r--p 00008000 103:03 23335669                  /usr/lib/gnome-settings-daemon/gsd-rfkill
556c910bc000-556c910bd000 rw-p 00009000 103:03 23335669                  /usr/lib/gnome-settings-daemon/gsd-rfkill
556c916af000-556c916f1000 rw-p 00000000 00:00 0                          [heap]
7f30d8000000-7f30d8021000 rw-p 00000000 00:00 0 
7f30d8021000-7f30dc000000 ---p 00000000 00:00 0 
7f30e0000000-7f30e0021000 rw-p 00000000 00:00 0 
7f30e0021000-7f30e4000000 ---p 00000000 00:00 0 
7f30e6f14000-7f30e6f15000 ---p 00000000 00:00 0 
7f30e6f15000-7f30e7715000 rw-p 00000000 00:00 0 
7f30e7715000-7f30e7716000 ---p 00000000 00:00 0 
7f30e7716000-7f30e7f16000 rw-p 00000000 00:00 0 
7f30e7f16000-7f30e89ff000 r--p 00000000 103:03 23462781                  /usr/lib/locale/locale-archive
7f30e89ff000-7f30e8a05000 r-xp 00000000 103:03 12976448                  /lib/x86_64-linux-gnu/libuuid.so.1.3.0
7f30e8a05000-7f30e8c04000 ---p 00006000 103:03 12976448                  /lib/x86_64-linux-gnu/libuuid.so.1.3.0
# This list goes on for a really long time
# format: start-end perm offset major:minor inode image
```

Each field in `/proc/*/maps` (except the image name) corresponds to a field in struct
vm_area_struct:

- `start`, `end`
  - The beginning and ending virtual addresses for this memory area
- `perm`
  - A bit mask with the memory area’s read, write, and execute permissions
  - Describes what the process is allowed to do with pages belonging to the area
  - The last character in the field is either p for “private” or s for “shared”
- `offset`
  - Where the memory area begins in the file that it is mapped to
  - An offset of 0 means that the beginning of the memory area corresponds to the beginning of the file
- `major`, `minor`
  - The major and minor numbers of the device holding the file that has been mapped
  - For device mappings the major and minor numbers refer to the disk partition holding the device special file that was opened by the user
     - NOT the device itself
 - `inode`
   - The inode number of the mapped file
 - `image`
   - The name of the file (usually an executable image) that has been mapped

#### The vm_area_struct Structure

When a user-space process calls mmap to map device memory into its address space,
the system responds by creating a new VMA to represent that mapping. A driver that supports mmap needs to be able to complete that intialization request. You should thus understand VMAs to support mmap properly. Let's look at the most important fields of `struct vm_area_struct`:

- `unsigned long vm_start;` and `unsigned long vm_end;`
  - The virtual address range covered by this VMA
  - First two fields shown in `/proc/*/maps`
- `struct file *vm_file;`
  - A pointer to the struct file structure associated with this area
- `unsigned long vm_pgoff;`
  - The offset of the area in the file, in pages
  - When a file or device is mapped, this is the file position of the first page mapped in this area
- `unsigned long vm_flags;`
  - A set of flags describing this VMA
  - We are most interested in e `VM_IO` and `VM_RESERVED`
    - `VM_IO` marks a VMA as being a memory-mapped I/O region
    - `VM_RESERVED` tells the memory management system not to attempt to swap out this VMA
- `struct vm_operations_struct *vm_ops;`
  - A set of functions that the kernel may invoke to operate on this memory area
- `void *vm_private_data;`
  - A field that may be used by the driver to store its own information

Pairing like fine wine with cheese is the `vm_operations_struct` with `vm_area_struct`. The `vm_operations_struct` includes the operations mentioned below:

- `void (*open)(struct vm_area_struct *vma);`
  - The open method is called by the kernel to allow the subsystem implementing the VMA to initialize the area
  - This method is invoked any time a new reference to the VMA is made 
  - Does NOT happen when the VMA is first created by mmap (mmap method called instead)
- `void (*close)(struct vm_area_struct *vma);`
  - When an area is destroyed, the kernel calls its close operation
- `struct page *(*nopage)(struct vm_area_struct *vma, unsigned long address, int *type);`
  - When a process tries to access a page that belongs to a valid VMA, but that is currently not in memory, the nopage method is called (if defined) for the related area
  - Returns a `struct page` pointer for the physical page
  - If the nopage method isn’t defined for the area, an empty page is allocated by the kernel
- `int (*populate)(struct vm_area_struct *vm, unsigned long address, unsigned long len, pgprot_t prot, unsigned long pgoff, int nonblock);`
  - Allows the kernel to “prefault” pages into memory before they are accessed by user space
  - Generally no need to implement this

### The Process Memory Map

The process memory map structure holds all of the other data structures together. Each process has a `struct mm_struct` defined in `<linux/sched.h>`. It contains the process’s list of virtual memory areas, page tables, and various other bits of memory management housekeeping information. It also contains a semaphore (mmap_sem) and a spinlock (page_table_lock). The pointer to this structure is found in the task structure. You shouldn't really ever need to access this. Now, time for mmap!

### The mmap Device Operation

Memory mapping can be implemented to provide user programs with direct access to device memory. Mapping a device means associating a range of user-space addresses to device memory. Whenever the program reads or writes in the assigned address range, it is actually accessing the device! This offers a huge performance increase over non-direct methods. 

Some devices don't need mmap - they are stream devices or serial ports. One other downside - a mapped region must be a multiple of PAGE_SIZE and must live in physical memoryat an address that is a multiple of PAGE_SIZE. This forces a larger memory region than absolutely required. One nice use of mmap is with graphics memory in graphics cards for the fastest speed possible. 

mmap is part of the file_operations structure. The prototype for the method is a lot different than the system call for mmap because a lot of setup is needed by the kernel before the method is invoked. Here is the system call of mmap:

```c
mmap (caddr_t addr, size_t len, int prot, int flags, int fd, off_t offset)
```

The file operation is declared as:

```c
int (*mmap) (struct file *filp, struct vm_area_struct *vma);
```

- `filp` is the same as in Ch. 3. 
- `vma` contains the information about the virtual address range that is used to access the device
- To implement mmap, a driver only has to build suitable page tables for the address range and (sometimes) replace `vma->vm_ops` with a new set of operations

There are two ways to build the page tables:

1. Doing it all at once with a function called `remap_pfn_range`
2. Doing it a page at a time via the nopage VMA method

Let's start with the all-at-once approach which is simpler:

#### Using remap_pfn_range

The job of building new page tables is handled by remap_pfn_range and io_remap_page_range:

```c
int remap_pfn_range(struct vm_area_struct *vma,
                    unsigned long virt_addr, unsigned long pfn,
                    unsigned long size, pgprot_t prot);
int io_remap_page_range(struct vm_area_struct *vma,
                        unsigned long virt_addr, unsigned long phys_addr,
                        unsigned long size, pgprot_t prot);
```

- Returns 0 on success and negative error codes
- `vma` is the virtual memory area into which the page range is being mapped
- `virt_addr` is the user virtual address where remapping should begin
- `pfn` is the page frame number corresponding to the physical address to which the virtual address should be mapped
  - The page frame number is simply the physical address right-shifted by PAGE_SHIFT bits
  - For most uses, the vm_pgoff field of the VMA structure contains exactly the value you need
- `size` is the dimension, in bytes, of the area being remapped
- `prot` is the protection requested for the new VMA
  - The driver should use the value found in vma->vm_page_prot
- The first function is intended for situations where pfn refers to actual system RAM
- The second function should be used when phys_addr points to I/O memory

### Mapping Memory with nopage

Sometimes you need a little more flexibility than just `remap_pfn_range`. This is when the nopage VMA method can be used. One useful situation for nopage is the mremap system call that changes the bounding addresses of a mapped region. If you want to grow a region, the nopage method is used. To support the mremap system call you need to implement the nopage method. 

When a user process attempts to access a page in a VMA that is not present in memory, the associated nopage function is called. The address parameter contains the virtual address that caused the fault, rounded down to the beginning of the page. The nopage function must locate and return the `struct page` pointer that refers to the page the user wanted. This function must also increment the usage count for the page it returns by calling the get_page macro:

```c
 get_page(struct page *pageptr);
```

- This is necessary to keep the reference counts correct on the mapped pages
- The kernel maintains this count for every page
  - When the count goes to 0, the kernel knows that the page may be placed on the free list
- When a VMA is unmapped, the kernel decrements the usage count for every page in the area
  - If you do not increment, system integrity is compromised

The nopage method should also store the type of fault in the location pointed to by
the type argument, but only if that argument is not NULL. For device drivers, the
proper value for type will be `VM_FAULT_MINOR`.

The nopage method normally returns a pointer to a struct page or returns a `NOPAGE_SIGBUS` error return. nopage can also return NOPAGE_OOM to indicate failures caused by resource limitations.

### Remapping Specific I/O Regions

A typical driver only wants to map the small address range that applies to its peripheral device, not all memory on the entire system. To do this, the driver only needs to play with offsets. The following code maps a region of simple_region_size bytes, beginning at physical address simple_region_start:

```c
unsigned long off = vma->vm_pgoff << PAGE_SHIFT;
unsigned long physical = simple_region_start + off;
unsigned long vsize = vma->vm_end - vma->vm_start;
unsigned long psize = simple_region_size - off;

if (vsize > psize)
    return -EINVAL; /* spans too high */
remap_pfn_range(vma, vma_>vm_start, physical, vsize, vma->vm_page_prot);
```

- `psize` is the physical I/O size that is left after the offset has been specified
- `vsize` is the requested size of virtual memory

Sometimes you want to prevent mapping extension instead of mapping additional area maps to the zero page that go beyond the physical memory region. The simplest way to prevent extension of the mapping is to implement a simple nopage method that always causes a bus signal to be sent to the faulting process:

```c
struct page *simple_nopage(struct vm_area_struct *vma,
                           unsigned long address, int *type);
{ return NOPAGE_SIGBUS; /* send a SIGBUS */}
```

This should only error if you are growing a region, but it is good to check anyways is to have the nopage method check to see whether a faulting address is within the device area. 

### Remapping RAM

One limitation of remap_pfn_range is that it gives access only to reserved pages and physical addresses above the top of physical memory. A page of physical addresses is marked as “reserved” in the memory map to indicate that it is not available for memory management, and reserved pages are thus locked in memory and are the only ones that can be safely mapped to user space for system stability.

remap_pfn_range won’t allow you to remap conventional addresses, which include the ones you obtain by calling get_free_page. Instead, it maps in the zero page. Everything appears to work, with the exception that the process sees private, zero-filled pages rather than the remapped RAM that it was hoping for. Yet, this function still does what most hardware drivers need it to do. It can remap high PCI buffers and PCI memory. 

### Remapping RAM with the nopage method

The best way to map real RAM to user space is to use `vm_ops->nopage` to deal with page faults one at a time. A sample implementation can be found in the scullp module. scullp is a page-oriented char device that can implement mmap on its memory. The implementation of scullp_mmap is very short, because it relies on the nopage function to do all the hard work:

```c
int scullp_mmap(struct file *filp, struct vm_area_struct *vma)
{
     struct inode *inode = filp->f_dentry->d_inode;
     
     /* refuse to map if order is not 0 */
     if (scullp_devices[iminor(inode)].order)
        return -ENODEV;
        
     /* don't do anything here: "nopage" will fill the holes */
     vma->vm_ops = &scullp_vm_ops;
     vma->vm_flags |= VM_RESERVED;
     vma->vm_private_data = filp->private_data;
     scullp_vma_open(vma);
     return 0;
}
```

open and close keep track of the mapping count:

```c
void scullp_vma_open(struct vm_area_struct *vma)
{
     struct scullp_dev *dev = vma->vm_private_data;
     
     dev->vmas++;
}
void scullp_vma_close(struct vm_area_struct *vma)
{
     struct scullp_dev *dev = vma->vm_private_data;
     
     dev->vmas--;
}
```

Alright now for the grunt work with the nopage method. In scullp, the `address` parameter to nopage is used to calculate an offset into the device, and the offset is used to look up the correct page in the memory tree. Here is the beast:

```c
struct page *scullp_vma_nopage(struct vm_area_struct *vma,
 unsigned long address, int *type)
 
{
     unsigned long offset;
     struct scullp_dev *ptr, *dev = vma->vm_private_data;
     struct page *page = NOPAGE_SIGBUS;
     void *pageptr = NULL; /* default to "missing" */
     
     down(&dev->sem);
     offset = (address - vma->vm_start) + (vma->vm_pgoff << PAGE_SHIFT);
     if (offset >= dev->size) goto out; /* out of range */
     
     /*
     * Now retrieve the scullp device from the list,then the page.
     * If the device has holes, the process receives a SIGBUS when
     * accessing the hole.
     */
     offset >>= PAGE_SHIFT; /* offset is a number of pages */
     for (ptr = dev; ptr && offset >= dev->qset;) {
         ptr = ptr->next;
         offset -= dev->qset;
     }
     if (ptr && ptr->data) pageptr = ptr->data[offset];
     if (!pageptr) goto out; /* hole or end-of-file */
     page = virt_to_page(pageptr);
     
     /* got it, now increment the count */
     get_page(page);
     if (type)
        *type = VM_FAULT_MINOR;
  out:
     up(&dev->sem);
     return page;
}
```

- scullp uses memory obtained with get_free_pages
- That memory is addressed using logical addresses, so all scullp_nopage has to do to get a struct page pointer is to call virt_to_page

### Remapping Kernel Virtual Addresses

It’s rarely necessary, but a driver can map a kernel virtual address to user space using mmap. Kernel virtual addresses are returned by a function like vmalloc. The driver scullv allocates its storage through vmalloc. The scullv version of nopage then looks like:

```c
     /*
     * After scullv lookup, "page" is now the address of the page
     * needed by the current process. Since it's a vmalloc address,
     * turn it into a struct page.
     */
     page = vmalloc_to_page(pageptr);
 
 /* got it, now increment the count */
     get_page(page);
     if (type)
        *type = VM_FAULT_MINOR;
out:
     up(&dev->sem);
     return page;
```

Side note: do NOT attempt to remap addresses from ioremap. They are special and cannot be treated like normal virtual addresses. Instead, use remap_pfn_range to remap I/O memory areas into user space.

### Performing Direct I/O

Most I/O ops are buffered though the kernel, but sometimes you want it to be transferred directly to/from user space. If the amount of data being transferred is large, transferring data directly without an extra copy through kernel space can speed things up. Most of the time, implementing direct I/O in a char driver is usually unnecessary and can be hurtful. You should take that step only if you are sure that the overhead of buffered I/O is truly slowing things down. Block and network drivers don't need to worry about this at all, because high-level code in the kernel will make use of direct access when indicated. I'm not going to go into much detail about performing direct I/O here, if you really need it then reference back to this chapter of the book for a detailed walkthrough of its operation. You will also need this for explicit asynchronous I/O in char drivers. 

### Direct Memory Access :open_mouth:

Direct memory access, or DMA, is the advanced topic that we end the chapter with. It is the hardware mechanism that allows peripheral components to transfer their I/O data directly to and from main memory without the need to involve the system processor (neat!). We do this to increase throughput to and from a device because it eliminates a lot of computational overhead. 

#### Overview of a DMA Data Transfer

Let's start by looking at input transfers. Data transfer can be triggered in two cases:

1. The software asks for data 
2. The hardware asynchronously pushes data to the system

Starting with case 1, here are the qualitative steps involved:

1. When a process calls read, the driver method allocates a DMA buffer and instructs the hardware to transfer its data into that buffer. The process is put to sleep.
2. The hardware writes data to the DMA buffer and raises an interrupt when it’s done.
3. The interrupt handler gets the input data, acknowledges the interrupt, and awakens the process, which is now able to read data.

Case 2 with asynchronous pushes happens (in one example) when data aquisition devices update data even when nobody is reading them. The steps for this type of transfer are:

1. The hardware raises an interrupt to announce that new data has arrived.
2. The interrupt handler allocates a buffer and tells the hardware where to transfer its data.
3. The peripheral device writes the data to the buffer and raises another interrupt when it’s done.
4. The handler dispatches the new data, wakes any relevant process, and takes care of housekeeping.

Overall, efficient DMA handling relies on good interrupt handling. Polling isn't used because that would waste the performance benefits over non-DMA operations that use processor I/O. DMA also requires device drivers to allocate special buffers for DMA. 

### Allocating the DMA Buffer

Although DMA buffers can be allocated either at system boot or at runtime, modules can allocate their buffers only at runtime. Make sure to use the right kind of memory - not all memory zones are good for DMA. High memory may not work for DMA on some systems where peripherals do not work with addresses that high. Use the `GFP_DMA` flag to kmalloc or get_free_pages call to get the right kind of memory. 

There are issues with allocating huge buffers for DMA. you can do it manually be securing top of RAM by allocating it at boot time. You can also do scatter/gather I/O - this is probably best for huge buffers. 

### Bus Addresses

A device driver using DMA needs to talk to hardware connected to the interface bus. This is different in that the bus uses physical addresses as opposed to program code that uses virtual addresses. DMA-based hardware actually uses a different type of address called a *bus* address. The kernel has two low level functions to convert between logical kernel addresses and bus address. Do not use these! There is a better, higher-level way. But you may see these in some kernel code:

```c
unsigned long virt_to_bus(volatile void *address);
void *bus_to_virt(unsigned long address);
```

Again, don't use these. Use the generic DMA layer.

### The Generic DMA Layer

DMA operations are essentially allocating a buffer and passing bus addresses to your device. Different systems have different ideas of how cache coherency should work so if you do not handle this issue correctly, your driver may corrupt memory. Some systems have complicated bus hardware that can make the DMA task easier or harder. Not all systems can perform DMA out of all parts of memory. 

The good news is that the kernel provides a bus- and architecture-independent DMA layer that hides most of these issues from you. Use this method! The following functions are included in `<linux/dma-mapping.h>`.

#### Dealing with difficult hardware (or children)

The kernel assumes your device can perform DMA to any 32-bit address. To change this, use the call:

```c
int dma_set_mask(struct device *dev, u64 mask);
```

- `mask` should show the bits that your device can address
  - some devices are limited to 24 bits, in which case you would use 0x0FFFFFF
- Returns nonzero if DMA is possible with given mask
- If 0, you are not able to use DMA on the device

Initialization would look something like this:

```c
if (dma_set_mask (dev, 0xffffff))
     card->use_dma = 1;
else {
     card->use_dma = 0; /* We'll have to live without DMA */
     printk (KERN_WARN, "mydev: DMA not supported\n");
}
```

You probably won't need to do this - especially if your device supports 32-bit addresses.

#### DMA Mappings

A DMA mapping is a combination of allocating a DMA buffer and generating an address for that buffer that is accessible by the device. A simple virt_to_bus call doesn't work well with IOMMU. So don't. Just don't.

Setting up a useful address for the device might require you to create a bounce buffer (like an inflatible castle at a kids birthday party - super fun). Bounce buffers are created when a driver attempts to perform DMA on an address that is not reachable by the peripheral device (like those stubborn high memory addresses). Data is copied to and from the bounce buffer as needed. Bounce buffers can slow things down, but sometimes it is your only hope.

DMA mappings also need to be careful of cache coherency. When your device reads from main memory, any changes to that memory residing in processor caches must be flushed out first. The generic DMA layer tries to help us as much as possible, but we will later introduce a small set of rules to follow to avoid these issues. 

DMA mapping sets up the new type `dma_addr_t` to represent bus addresses. Let's look at two types of DMA mappings - they differ in usage based on how long you need them to hang around.

- Coherent DMA mappings
  - These mappings usually exist for the life of the driver
  - Simultaneously available to both the CPU and the peripheral
  - Live in cache-coherent memory
  - Can be expensive to set up and use
- Streaming DMA mappings
  - Usually set up for a single operation
  - Use these whenever possible
    - They don't monopolize mapping registers for a long time
    - Can be optimized in ways that are not available to coherent

These mappings are manipulated in different ways. 

#### Coherent DMA Mappings

A coherent mapping can be set up with a call to dma_alloc_coherent like this:

```c
void *dma_alloc_coherent(struct device *dev, size_t size,
                         dma_addr_t *dma_handle, int flag);
```

- This function handles both the allocation and the mapping of the buffer
- Return value is a kernel virtual address for the buffer
- The associated bus address is returned in `dma_handle`
- Use `GFP_KERNEL` for the flag argument

You can then return the buffer to the system when no longer needed with:

```c
void dma_free_coherent(struct device *dev, size_t size,
                       void *vaddr, dma_addr_t dma_handle);
```

A DMA pool is an allocation mechanism for small, coherent DMA mappings. Mappings obtained from dma_alloc_coherent may have a minimum size of one page. If you need a smaller size than that, use a DMA pool. They are nice for small DMA operations embedded in larger structures. A DMA pool can be created with:

```c
struct dma_pool *dma_pool_create(const char *name, struct device *dev,
                                 size_t size, size_t align,
                                 size_t allocation);
```

- `name` is the name for the pool
- `dev` is your device structure
- `size` is the size of the buffers to be allocated from this pool
- `align` is the required hardware alignment for allocations from the pool
- `allocation` is a memory boundary that allocations should not exceed

You can free a pool with:

```c
void dma_pool_destroy(struct dma_pool *pool);
```

Return all allocations to the pool before destroying it! Allocations are done with:

```c
void *dma_pool_alloc(struct dma_pool *pool, int mem_flags, dma_addr_t *handle);
```

- `mem_flags` is the usual set of GFP_ allocation flags

Buffers can be returned to the pool with:

```c
void dma_pool_free(struct dma_pool *pool, void *vaddr, dma_addr_t addr);
```

#### Streaming DMA Mappings

These are a lot more complicated to setup than coherent ones. First, let's review some symbols that help us tell the kernel about the direction of data flow:

- `DMA_TO_DEVICE` and `DMA_FROM_DEVICE`
  - Use the first one if data is being sent to the device
  - Use the second one is data is going to the CPU
- `DMA_BIDIRECTIONAL`
  - For data that can move in either direction
- `DMA_NONE`
  - This symbol is provided only as a debugging aid
  - It makes the kernel panic. The kernel is always nervous haha

Resist always using the bidirectional version - it may have worse performance on some systems. When you have a single buffer to transfer, map it with this:

```c
dma_addr_t dma_map_single(struct device *dev, void *buffer, size_t size,
                          enum dma_data_direction direction);
```

The return value is the bus address that you can pass to the device or NULL if an error happens. Once the transfer is complete, the mapping should be deleted with:

```c
void dma_unmap_single(struct device *dev, dma_addr_t dma_addr, size_t size,
                      enum dma_data_direction direction);
```

Important rules for streaming DMA mappings:

- The buffer must be used only for a transfer that matches the direction value given when it was mapped
- Once a buffer has been mapped, it belongs to the device, not the processor. Until the buffer has been unmapped, the driver should not touch its contents in any way
- The buffer must not be unmapped while DMA is still active, or serious system instability is guaranteed

Occasionally a driver needs to access the contents of a streaming DMA buffer without unmapping it (arg!). A call has been provided to make this possible:

```c
void dma_sync_single_for_cpu(struct device *dev, dma_handle_t bus_addr,
                             size_t size, enum dma_data_direction direction);
```

Before the device accesses the buffer, ownership should be transferred back to it with:

```c
void dma_sync_single_for_device(struct device *dev, dma_handle_t bus_addr,
                                size_t size, enum dma_data_direction direction);
```

The processor should NOT access the DMA buffer after this call has been made.

##### Single-page streaming mappings

You might want to set up a mapping on a `struct page` pointer. This can happen with user-space buffers mapped with get_user_pages. You can set up and unmap streaming mappings with page pointers with:

```c
dma_addr_t dma_map_page(struct device *dev, struct page *page,
                        unsigned long offset, size_t size,
                        enum dma_data_direction direction);
 
void dma_unmap_page(struct device *dev, dma_addr_t dma_address,
                    size_t size, enum dma_data_direction direction);
```

##### Scatter/gather mappings 

These are a special type of DMA mapping. You can map a bunch of buffers at once - each with a required function. Many devices can accept a scatterlist of array pointers and lengths and transfer them all in one DMA operation. This can let you turn multiple operations into a single DMA and speed up performance. The first step to using these is to create and fill an array of `struct scatterlist` describing the buffers to be transferred. The structure contains three fields:

- `struct page *page;`
  - The struct page pointer corresponding to the buffer to be used in the scatter/gather operation.
- `unsigned int length;` and `unsigned int offset;`
  - The length of that buffer and its offset within the page

To map a scatter/gather DMA operation, the driver needs to set the page, offset, and length fields in a `struct scatterlist` entry for each buffer to be transferred. Then you call:

```c
int dma_map_sg(struct device *dev, struct scatterlist *sg, int nents,
               enum dma_data_direction direction)
```

- `nents` is the number of scatterlist entries passed in
- Return value is the number of DMA buffers to transfer (it could be less than nents)

For each buffer in the input scatterlist, dma_map_sg determines the proper bus address to give to the device. As part of that task, it also coalesces buffers that are adjacent to each other in memory. You will never know what the resulting transfer will look like, however, until after the call. To transfer the buffer, use:

```c
dma_addr_t sg_dma_address(struct scatterlist *sg);
    // Returns the bus (DMA) address from this scatterlist entry.
unsigned int sg_dma_len(struct scatterlist *sg);
    // Returns the length of this buffer.
```

Once the transfer is complete, a scatter/gather mapping is unmapped with:

```c
void dma_unmap_sg(struct device *dev, struct scatterlist *list,
                  int nents, enum dma_data_direction direction);
```

Scatter/gather mappings are streaming DMA mappings. The same access rules apply to them as to the single variety. If you must access a mapped scatter/gather list, you must synchronize it first with:

```c
void dma_sync_sg_for_cpu(struct device *dev, struct scatterlist *sg,
                         int nents, enum dma_data_direction direction);
void dma_sync_sg_for_device(struct device *dev, struct scatterlist *sg,
                            int nents, enum dma_data_direction direction);
```

There are a lot more specific DMA operations for PCI and ISA devices - I won't be getting into the specifics of them here, but know that there is a lot more info for working with a particular type of bus with DMA operations. 