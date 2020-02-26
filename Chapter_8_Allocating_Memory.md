# LDD3 Chapter 8 Notes

## Chapter 8: Allocating Memory

Chapter goal: learn the kernel set of memory primitives

So far we have used kmalloc() and kfree() to allocate and free memory. The kernel offers much better memory management tools though, as this chapter will describe in more detail. 

### The Real Story of kmalloc

kmalloc is powerful and simple to use because of its similarity to malloc. It is fast and does not clear the memory it obtains. The allocated region is also contiguous in physical memory. Here are more details on kmalloc:

The prototype for kmalloc is:

```c
#include <linux/slab.h>
void *kmalloc(size_t size, int flags);
```

- The first argument is the size of the block to be allocated
- The second argument controls to behavior of kmalloc in a number of different ways

The most commonly used flag is `GFP_KERNEL` and it means that the calling function is executing a system call on behalf of a process. This means kmalloc can put the current process to sleep waiting for a page when called in low-memory situations. 

`GFP_KERNEL` is not always the best option, however, because sometimes kmalloc is called from outside a process’s context. This can happen with interrupt handlers, tasklets, and kernel timers. In this case we do not want to sleep the process and the driver should use a flag of `GFP_ATOMIC`. `GFP_ATOMIC` will use the last free page, and will fail if the allocation could not happen. 

There are symbols that represent frequently used combinations of flags. These are:

```
GFP_ATOMIC
    Used to allocate memory from interrupt handlers and other code outside of a
    process context. Never sleeps.
    
GFP_KERNEL
    Normal allocation of kernel memory. May sleep.
    
GFP_USER
    Used to allocate memory for user-space pages. It may sleep.
    
GFP_HIGHUSER
    Like GFP_USER, but allocates from high memory, if any. High memory is
    described in the next subsection.
    
GFP_NOIO
GFP_NOFS
    These flags function like GFP_KERNEL, but they add restrictions on what the kernel 
    can do to satisfy the request. A GFP_NOFS allocation is not allowed to perform any 
    filesystem calls, while GFP_NOIO disallows the initiation of any I/O at all.
    They are used primarily in the filesystem and virtual memory code where an 
    allocation may be allowed to sleep, but recursive filesystem calls would be a bad 
    idea.
```

You can also use a boolean OR operator with any of the following flags:

```
__GFP_DMA
    This flag requests allocation to happen in the DMA-capable memory zone. The
    exact meaning is platform-dependent and is explained in the following section.

__GFP_HIGHMEM
    This flag indicates that the allocated memory may be located in high memory.
    
__GFP_COLD
    Normally, the memory allocator tries to return “cache warm” pages—pages that
    are likely to be found in the processor cache. Instead, this flag requests a 
    “cold” page, which has not been used in some time. It is useful for allocating 
    pages for DMA reads, where presence in the processor cache is not useful.
   
__GFP_NOWARN
    This rarely used flag prevents the kernel from issuing warnings (with printk)
    when an allocation cannot be satisfied.
    
__GFP_HIGH
    This flag marks a high-priority request, which is allowed to consume even the
    last pages of memory set aside by the kernel for emergencies.
    
__GFP_REPEAT
__GFP_NOFAIL
__GFP_NORETRY
   These flags modify how the allocator behaves when it has difficulty satisfying an
   allocation. __GFP_REPEAT means “try a little harder” by repeating the attempt—
   but the allocation can still fail. The __GFP_NOFAIL flag tells the allocator   
   never to fail. It works as hard as needed to satisfy the request. __GFP_NORETRY 
   tells the allocator to give up immediately if the requested memory is not 
   available.      
```

#### Memory Zones

Both `__GFP_DMA` and `__GFP_HIGHMEM` have a platform-dependent role, although their
use is valid on all platforms.

The Linux kernel knows about a minimum of three memory zones:

1. DMA-capable memory
2. normal memory
3. high memory

While allocation happens in the `normal` memory zone most of the time, setting either of the bits just mentioned requires memory to be allocated from a different zone. We have special memory ranges that fall into the abstraction (a higher abstraction layer than considering all RAM equivalents).

DMA-capable memory is memory that lives in a preferential address range where peripherals can perform DMA access. On most sane platforms, all memory lives in
this zone. (what if we are insane???)

High memory is a mechanism used to allow access to (relatively) large amounts of
memory on 32-bit platforms. Only use this if you need a large section of memory - it is a little trickier to work with.

Whenever a new page is allocated to fulfill a memory allocation request, the kernel
builds a list of zones that can be used in the search. With `__GFP_DMA` specified, only the DMA zone is searched. Different flags can present different combinations of zones to look at. It is important to note that kmalloc cannot allocate high memory. 

In NUMA systems, the allocator attempts to locate memory local to the processor performing the allocation, but there are ways to change this. 

This will be visited again in chapter 15.

### The Size Argument

The kernel takes care of managing the system's physical memory, which is available only in page-sized chunks. Thus, kmalloc is different than normal malloc from user space programs. A simple heap-oriented allocation technique would very quickly run into trouble if everyone needed their own page for everything. 

Linux handles memory allocation by creating a set of pools of memory objects of
fixed sizes. Allocation requests are handled by going to a pool that holds sufficiently large objects and handing an entire memory chunk back to the requester. This technique is very complex, and normally we don't care very much about it with respect to device drivers. It is important to remember that the kernel can allocate only certain predefined, fixed-size byte arrays. The smallest allocation that kmalloc can handle might be 32 or 64 bytes depending on the page size of the system. 

There is also a maximum amount of memory that kmalloc can allocate at once, maybe around 128kB (but there are better ways to allocate larger chuncks that this chapter will cover later). 

### Lookaside Caches

A device driver often ends up allocating many objects of the same size, over and over again. We can create special pools of this size called a lookaside cache. Drivers usually do not use these, but USB and SCSI do so we will look at it. 

The cache allocator in Linux is sometimes called a "slab allocator". This is why the functions are declared in `<linux/slab.h>`. The slab allocator implements caches that have a type `kmem_cache_t` and are created with a call to `kmem_cache_create`:

```c
kmem_cache_t *kmem_cache_create(const char *name, size_t size,
     size_t offset,
     unsigned long flags,
     void (*constructor)(void *, kmem_cache_t *,
                       unsigned long flags),
     void (*destructor)(void *, kmem_cache_t *,
                     unsigned long flags));
```

Wow that is a big constructor!

- The function creates a new cache object that can host any number of memory areas all of the same size specified by the `size` argument
- The `name` argument is associated with this cache and functions as housekeeping information usable in tracking problems
  - Usually it is set to the name of the type of structure that is cached
- The `offset` is the offset of the first object in the page. You will probably use 0 to request the default value. 
- `flags` controls how allocation is done and is a bit mask of the following flags:

```
SLAB_NO_REAP
    Setting this flag protects the cache from being reduced when the system is 
    looking for memory. Setting this flag is normally a bad idea; it is important to 
    avoid restricting the memory allocator’s freedom of action unnecessarily.

SLAB_HWCACHE_ALIGN
    This flag requires each data object to be aligned to a cache line; actual 
    alignment depends on the cache layout of the host platform. This option can be a 
    good choice if your cache contains items that are frequently accessed on SMP
    machines. The padding required to achieve cache line alignment can end up
    wasting significant amounts of memory, however.
    
SLAB_CACHE_DMA
    This flag requires each data object to be allocated in the DMA memory zone.
```

The constructor and destructor arguments to the function are optional functions but must both be used (you can't just use one or the other). They can be useful, but there are a few constraints you need to keep in mind:

- A constructor is called when the memory for a set of objects is allocated
  - because that memory may hold several objects, the constructor may be called multiple times
- You cannot assume that the constructor will be called as an immediate effect of allocating an object
- Destructors can be called at some unknown future time
  - not immediately after an object has been freed
- Constructors and destructors may or may not be allowed to sleep
  - This is according to whether they are passed the SLAB_CTOR_ATOMIC flag

Once a cache of objects is created, you can allocate objects from it by calling `kmem_cache_alloc`. The prototype is:

```c
void *kmem_cache_alloc(kmem_cache_t *cache, int flags);
```

The `cache` argument is the cache you have created previously and the flags are the same as the ones you would pass to kmalloc. To free an object, use `kmem_cache_free` with the following prototype:

```c
void kmem_cache_free(kmem_cache_t *cache, const void *obj);
```

When the module is unloaded, it should free the cache with:

```c
 int kmem_cache_destroy(kmem_cache_t *cache);
```

The destroy operation succeeds only if all objects allocated from the cache have been returned to it, so you should check this value as a failure would indicate a leak within the module. 

### A scull Based on the Slab Caches: scullc  :skull:

scullc is a trimmed-down version of the scull module that implements only the bare device, which is the persistent memory region. While scull uses kmalloc, scullc uses memory caches. 

First, it needs to declare the slab cache:

```c
/* declare one cache pointer: use it for all devices */
kmem_cache_t *scullc_cache;
```

The creation of the slab cache is handled in this way:

```c
/* scullc_init: create a cache for our quanta */
scullc_cache = kmem_cache_create("scullc", scullc_quantum,
    0, SLAB_HWCACHE_ALIGN, NULL, NULL); /* no ctor/dtor */
    
if (!scullc_cache) {
    scullc_cleanup();
    return -ENOMEM;
}
```

And allocating memory quanta (the made up unit of space):

```c
/* Allocate a quantum using the memory cache */
if (!dptr->data[s_pos]) {
     dptr->data[s_pos] = kmem_cache_alloc(scullc_cache, GFP_KERNEL);
     if (!dptr->data[s_pos])
        goto nomem;
     memset(dptr->data[s_pos], 0, scullc_quantum);
}
```

And releasing the memory as:

```c
for (i = 0; i < qset; i++)
if (dptr->data[i])
    kmem_cache_free(scullc_cache, dptr->data[i]);
```

Then, at unload time, we return the cache to the system with:

```c
/* scullc_cleanup: release the cache of our quanta */
if (scullc_cache)
    kmem_cache_destroy(scullc_cache);
```

This method is more efficient and slightly faster than kmalloc. 

### Memory Pools

There are places in the kernel where memory allocations cannot be allowed to fail. TO guarantee allocations, developers created an abstraction known as a memory pool. It is really just a form of a lookaside cache that tries to always keep a list of free memory around for use in emergencies. How nice of it to do that.

A memory pool has a type of `mempool_t` defined in `<linux/mempool.h>`. You can create one with `mempool_create` as follows:

```c
mempool_t *mempool_create(int min_nr,
     mempool_alloc_t *alloc_fn,
     mempool_free_t *free_fn,
     void *pool_data);
```

- `min_nr` is the minimum number of allocated objects that the pool should always keep around
- actual allocation and freeing of objects is handled by alloc_fn and free_fn with the following prototypes:

```c
typedef void *(mempool_alloc_t)(int gfp_mask, void *pool_data);
typedef void (mempool_free_t)(void *element, void *pool_data);
```

- And the final parameter to mempool_create (pool_data) is passed to alloc_fn and free_fn.

There are two functions (mempool_alloc_slab and mempool_free_slab) that perform the impedance matching between the memory pool allocation prototypes and kmem_cache_alloc and kmem_cache_free. Code that sets up memory pools often looks like this:

```c
cache = kmem_cache_create(. . .);
pool = mempool_create(MY_POOL_MINIMUM,
             mempool_alloc_slab, mempool_free_slab,
             cache);
```

When the pool has been created, objects can be allocated and freed with:

```c
void *mempool_alloc(mempool_t *pool, int gfp_mask);
void mempool_free(void *element, mempool_t *pool);
```

When a mempool is created, there will be a pool of pre-allocated objects created that can be used should future calls to mempool_alloc fail. When one of these preallocated pools is freed with mempool free, it is kept in the pool. This also happens if a successfully initialized pool is freed but the total number of preallocated objects is currently below some minimum value. A mempool can be resized with the following call:

```c
int mempool_resize(mempool_t *pool, int new_min_nr, int gfp_mask);
```

- If successful, it resizes the pool to have at least `new_min_nr` objects. 

A mempool can be returned to the system with:

```c
void mempool_destroy(mempool_t *pool);
```

- Remember to return all allocated objects before destroying a mempool.

Using mempools is drivers should be rare because they consume a lot of memory. Allocating and dealing with allocating failures is probably the better route to go.

### get_free_page and Friends (friends? what are those?)

If your module needs a lot of memory, it is better to use a page-oriented technique instead of mempools. To allocate entire pages, use the following:

```c
get_zeroed_page(unsigned int flags);
    // Returns the pointer to a new page and fills the page with zeros.
    
__get_free_page(unsigned int flags);
    // Similar to get_zeroed_page, but doesn’t clear the page. This seems like a security bug to me.
    
__get_free_pages(unsigned int flags, unsigned int order);
    // Allocates and returns a pointer to the first byte of a memory area that is
    // potentially several (physically contiguous) pages long but doesn’t zero the area.
```

- `flags` argument is similar to kmalloc. Use `GFP_KERNEL` or `GFP_ATOMIC` and maybe add the `__GFP_DMA` flag for direct-memory-access operations or `__GFP_HIGHMEM` when high memory can be used
- `order` is the  base-two logarithm of the number of pages you are requesting or freeing. If order is too big, allocation fails (usually is 10 or 11)

You can free pages with either of the following functions:

```c
void free_page(unsigned long addr); // this is a macro of the function below it
void free_pages(unsigned long addr, unsigned long order);
```

### A scull Using Whole Pages: scullp

Memory quanta allocated in the scullp module are whole pages or page sets. The `scullp_order` variable defaults to 0 but can be changed. This is how scullp allocates and deallocates memory:

```c
/* Here's the allocation of a single quantum */
if (!dptr->data[s_pos]) {
    dptr->data[s_pos] =
        (void *)__get_free_pages(GFP_KERNEL, dptr->order);
    if (!dptr->data[s_pos])
        goto nomem;
    memset(dptr->data[s_pos], 0, PAGE_SIZE << dptr->order);
}

/* This code frees a whole quantum-set */
for (i = 0; i < qset; i++)
    if (dptr->data[i])
        free_pages((unsigned long)(dptr->data[i]),
            dptr->order);
```

The main advantage of this method over kmalloc is efficiency of memory usage. Allocating by pages wastes no memory, while kmalloc wastes an unpredictable amount of memory because of allocation granularity.

### The alloc_pages Interface

The real core of the Linux page allocator is the function alloc_pages_node with prototypes:

```c
struct page *alloc_pages_node(int nid, unsigned int flags,
                              unsigned int order);

// Additional macro versions:
struct page *alloc_pages(unsigned int flags, unsigned int order);
struct page *alloc_page(unsigned int flags);
```

- `nid` is the NUMA node ID
- `flags` is the usual `GFP_` allocation flags
- `order` is the size of the allocation (again, in 2^order format)
- The return value is a pointer to the first page structure of the allocated memory or NULL for a failed allocation
- `alloc_pages` simplifies this by putting the memory on the current NUMA node
- `alloc_page` only allocates one page

To release pages, you should use one of these functions:

```c
void __free_page(struct page *page);
void __free_pages(struct page *page, unsigned int order);
void free_hot_page(struct page *page);
void free_cold_page(struct page *page);
```

- The hot page versions are for cache-resident pages

### vmalloc and Friends (again, where are these friends coming from?)

vmalloc allocates a contiguous memory region in *virtual* address space that might be nonconsecutive in physical memory. It returns 0 if an error occurs, and a pointer to a linear memory area of size at least `size` on success. You shouldn't choose vmalloc as your first choice in development because it is slightly less efficient to work with. Try to work with individual pages rather than vmalloc. But here are the prototypes for vmalloc:

```c
#include <linux/vmalloc.h>

void *vmalloc(unsigned long size);
void vfree(void * addr);
void *ioremap(unsigned long offset, unsigned long size);
void iounmap(void * addr);
```

Important notes on vmalloc:

- Remember: addresses returned by kmalloc, `_get_free_pages`, and vmalloc are virtual addresses
- kmalloc and `_get_free_pages` have a one-to-one mapping to physical memory while vmalloc and ioremap do not follow this
- Use vmalloc for allocating memory for a large sequential buffer that exists only in software
- Memory allocated with vmalloc is released by vfree
- ioremap builds new page tables like vmalloc but does not actually allocate any memory
  - The return value of ioremap is a special virtual address
  - The virtual address obtained is eventually released by calling iounmap
  - ioremap is most useful for mapping the (physical) address of a PCI buffer to (virtual) kernel space
- vmalloc cannot be used in atomic context!

### A scull that uses Virtual Addresses: scullv

The module scullv provides sample code for using vmalloc. The module allocates memory 16 pages at a time. The only difference between scullv and scullp is allocation management. 

How scullv obtains new memory and releases it:

```c
/* Allocate a quantum using virtual addresses */
if (!dptr->data[s_pos]) {
    dptr->data[s_pos] =
        (void *)vmalloc(PAGE_SIZE << dptr->order);
    if (!dptr->data[s_pos])
        goto nomem;
    memset(dptr->data[s_pos], 0, PAGE_SIZE << dptr->order);
}

/* Release the quantum-set */
for (i = 0; i < qset; i++)
    if (dptr->data[i])
        vfree(dptr->data[i]);
```

### Per-CPU Variables

When you create a per-CPU variable, each processor on the system gets its own copy of that variable. This is nice because:

- It requires almost no locking because each CPU works with its own copy
- Per-CPU variables can remain in their respective processors’ caches to speed up programs

A good example of these variables is found in networking drivers. Rather than deal with the caching and locking issues with the many counters in networking, the we put the statistics counters into per-CPU variables. Updates are lockless and fast. On the rare occasion that user space requests to see the values of the counters, you can simply add up each processor’s version and return the total.

Declarations for per-CPU variables are found in `<linux/percpu.h>` and they can be created with the following macro:

```c
DEFINE_PER_CPU(type, name);

// Can also be used with an array like:
DEFINE_PER_CPU(int[3], my_percpu_array);
```

The main locking feature needed with these variables is to use the `get_cpu_var` macro to access the current processor’s copy of a given variable, and call `put_cpu_var` when you are done. `get_cpu_var` returns an lvalue for the current processor’s version of the variable and disables preemption. The lvalue can also be changed directly. For example:

```c
get_cpu_var(sockets_in_use)++;
put_cpu_var(sockets_in_use);

// And you can access another processors copy with:
per_cpu(variable, int cpu_id);
```

You can dynamically allocate per-CPU variables with:

```c
void *alloc_percpu(type); //works most of the time
void *__alloc_percpu(size_t size, size_t align);
```

- The variables can be freed with a call to free_percpu
- Access to these variables is done with:

```c
per_cpu_ptr(void *per_cpu_var, int cpu_id);
```

This macro returns a pointer to the version of `per_cpu_var` corresponding to the given `cpu_id`.

Code using dynamic per-CPU variables tends to look like this:

```c
int cpu;
cpu = get_cpu( )
ptr = per_cpu_ptr(per_cpu_var, cpu);
/* work with ptr */
put_cpu( );
```

Per-CPU variables can be exported to modules, but you must use a special version of the macros:

```c
EXPORT_PER_CPU_SYMBOL(per_cpu_var);
EXPORT_PER_CPU_SYMBOL_GPL(per_cpu_var);

// And to access these variables within a module, declare it with:
DECLARE_PER_CPU(type, name);
```

There is usually not much space for per-CPU variables in a system, so keep them small!

### Obtaining Large Buffers (getting super buff requires heavy lifting)

Allocations of large, contiguous memory buffers are prone to failure. So - before you go about trying to allocate a huge region of memory, look at scatter/gather operations in chapter 1. 

#### Acquiring a Dedicated Buffer at Boot Time

If you really need to do this - you should request it at boot time to have the best odds at actually getting a sufficient memory chunck free. This is the dirty way to do it but can be less prone to failure. A module cannot allocate memory at boot time - only drivers linked directly to the kernel can do that. 

Boot time allocation is performed with one of the following functions:

```c
#include <linux/bootmem.h>

void *alloc_bootmem(unsigned long size);
void *alloc_bootmem_low(unsigned long size);
void *alloc_bootmem_pages(unsigned long size);
void *alloc_bootmem_low_pages(unsigned long size);
```

Use the low versions if you need DMA operations in your driver. Usually you do not free this memory, but there is a mechanism to do it with:

```c
void free_bootmem(unsigned long addr, unsigned long size);
```

It is probably best to avoid this technique altogether for now. Only use it if you *really* *really* need to. And then go to the internet to find out how to best do this. 
