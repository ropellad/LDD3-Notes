# LDD3 Chapter 16 Notes

## Chapter 16: Block Drivers

Chapter goal: Understand what a block device, block driver, and ramdisk are.

A block driver provides access to devices that transfer randomly accessible data in
fixed-size blocks. These are mainly disk drives, SSDs, and portable USB flash drives. The kernel sees block devices as fundamentally different than char devices, so they have their own interface. 

Block drivers are really important for overall system performance. Since virtual memory works by shifting unneeded data to secondary storage (usually a much slower SSD or gulp... a spinning disk drive!). Block drivers are really part of the virtual memory subsytem, so we need easy ways to make these things fast.

The driver we will look at is a simple ramdisk implementation called sbull. First, let's define a few terms:

- Block: a fixed-size chunk of data, the size being determined by the kernel
  - Often 4096 bytes. but can vary
- Sector: a small block whose size is usually determined by the underlying hardware
  - Usually 512 bytes
  - If different, the kernel sector number needs to be scaled accordingly 

### Registration

Block drivers must use a set of registration interfaces to make their devices available to the kernel. These are totally different than char devices. 

Step 1 is to register the driver with the kernel with this function:

```c
int register_blkdev(unsigned int major, const char *name);
```

- You need to pass the major number of the device and the associated name
  - If major number is set to 0, the kernel allocates a new one for you

The following function cancels a block driver registration:

```c
int unregister_blkdev(unsigned int major, const char *name);
```

Block devices make their operations available to the filesystem with `struct block_device_operations` declared in `<linux/fs.h>`. Fields in this structure include:

- `int (*open)(struct inode *inode, struct file *filp);` and
- `int (*release)(struct inode *inode, struct file *filp);`
  - Functions that are called whenever the device is opened and closed
  - Can do things like spinning up the disks or closing a disk tray
- `int (*ioctl)(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg);`
  - Method that implements the ioctl system call
  - The block layer first intercepts a large number of standard requests
    - Thus, mmost block driver ioctl methods are fairly short
- `int (*media_changed) (struct gendisk *gd);`
  - Method called by the kernel to check whether the user has changed the media in the drive, returning a nonzero value if true
  - Only applicable to drives that support removable media 
- `int (*revalidate_disk) (struct gendisk *gd);`
  - Called in response to a media change
  - Gives the driver a chance to perform whatever work is required to make the new media ready for use
  - Return value is ignored by the kernel
- `struct module *owner;`
  - A pointer to the module that owns this structure
  - Usually initialized to THIS_MODULE

#### The gendisk Structure

`struct gendisk` is the kernel's representation of an individual disk device. gendisk is also used to represent paritions, but don't worry about those. The fields you need to initialize in the `struct gendisk` with a block driver:

- `int major;` and `int first_minor;` and `int minors;`
  - Fields that describe the device number(s) used by the disk
  - Must use at least one minor number
  - Use one minor number for each partition
  - Common to use 16 or 64 for `minors`
- `char disk_name[32];`
  - Set to the name of the disk device
  - Shows up in /proc/partitions and sysfs
- `struct block_device_operations *fops;`
  - Set of device operations from the previous section
- `struct request_queue *queue;`
  - Structure used by the kernel to manage I/O requests for this device
- 'int flags;'
  - Set of flags describing the state of the drive
  - Set GENHD_FL_REMOVABLE for removable media
  - Set GENHD_FL_CD for CD-ROM drives
- `sector_t capacity;`
  - Capacity of the drive, in 512-byte sectors
  - The sector_t type can be 64 bits wide
  - Don't set this directly, pass number of sectors to set_capacity
- `void *private_data;`
  - Block drivers may use this field for a pointer to their own internal data (shh, its a secret)

The kernel provides a set of functions to work with gendisk structures. Since gendisk is dynamically allocated and requires special kernel manipulation for initialization, drivers cannot allocate the structure on their own. Instead, they must call:

```c
struct gendisk *alloc_disk(int minors);
```

- `minors` should be the number of minor numbers this disk uses
- Don't change this value later

When a disk is no longer needed, it should be freed with:

```c
void del_gendisk(struct gendisk *gd);
```

- Make sure to delete the structure when there are no users after the final release or in the module cleanup function to avoid issues with your methods being called after disk deletion

To make a disk available to the system after allocating it, intializae the structure with:

```c
void add_disk(struct gendisk *gd);
```

- Don't call this until you are completely ready to go!

### Initialization in sbull

sbull implements a set of in-memory virtual disk drives. For each drive, sbull allocates an array of memory with vmalloc and makes it available via block operations. You can specify a major number for the device, or have one set dynamically. sbull does it like this:

```c
sbull_major = register_blkdev(sbull_major, "sbull");
if (sbull_major <= 0) {
     printk(KERN_WARNING "sbull: unable to get major number\n");
     return -EBUSY;
}
```

The sbull device is desribed by an internal structure:

```c
struct sbull_dev {
     int size;                    /* Device size in sectors */
     u8 *data;                    /* The data array */
     short users;                 /* How many users */
     short media_change;          /* Flag a media change? */
     spinlock_t lock;             /* For mutual exclusion */
     struct request_queue *queue; /* The device request queue */
     struct gendisk *gd;          /* The gendisk structure */
     struct timer_list timer;     /* For simulated media changes */
};
```

To initialize the device, first start by intializing and allocating the underlying memory:

```c
memset (dev, 0, sizeof (struct sbull_dev));
dev->size = nsectors*hardsect_size;
dev->data = vmalloc(dev->size);
if (dev->data = = NULL) {
     printk (KERN_NOTICE "vmalloc failure.\n");
     return;
}
spin_lock_init(&dev->lock);
```

Before allocating the request queue, we need to first allocate and initialize a spinlock (notes go into this more later):

```c
dev->queue = blk_init_queue(sbull_request, &dev->lock);
```

- sbull_request is the request function - it is the one that performs the physical read/write requests. The spinlock controls access to that queue. 

Next, we can allocate, initialize, and install the corresponding gendisk structure with:

```c
dev->gd = alloc_disk(SBULL_MINORS);
if (! dev->gd) {
     printk (KERN_NOTICE "alloc_disk failure\n");
     goto out_vfree;
}
dev->gd->major = sbull_major;
dev->gd->first_minor = which*SBULL_MINORS;
dev->gd->fops = &sbull_ops;
dev->gd->queue = dev->queue;
dev->gd->private_data = dev;
snprintf (dev->gd->disk_name, 32, "sbull%c", which + 'a');
set_capacity(dev->gd, nsectors*(hardsect_size/KERNEL_SECTOR_SIZE));
add_disk(dev->gd);
```

- `SBULL_MINORS` is the number of minors that each sbull device supports
- When finished, we call add_disk (LAST STEP)

### Sector Sizes

The kernel treats every disk as a linear array of 512-byte sectors. Not all devices have that sector size, though, and a few details need to be taken care of to get that device to work. sbull has a `hardsect_size` parameter that can be used to change the hardware sector size of the device - look at this to support it in your drivers. 

First detail to look at: inform the kernel of the sector size your device supports. It is a parameter of the request queue. Do this with:

```c
blk_queue_hardsect_size(dev->queue, hardsect_size);
```

- Once that is done, the kernel adheres to your device’s hardware sector size
- But kernel always expresses itself in 512-byte sectors
  -  translate all sector numbers accordingly

When sbull sets the capacity of the device in its gendisk structure, the call looks like:

```c
set_capacity(dev->gd, nsectors*(hardsect_size/KERNEL_SECTOR_SIZE));
```

- KERNEL_SECTOR_SIZE is a locally-defined constant to scale between the kernel’s 512-byte sectors and whatever size we have been told to use
- This calculation pops up frequently in the sbull request processing logic

### The Block Device Operations

The sbull device simulates itself as a removable device. When the last user closes the device, a 30 second timer is set. If the device is not opened during that time, the contents of the device are cleared and the kernel is told that the removable media has been changed. This delay gives users time to mount an sbull device after creating a filesystem on it. 

### The open and release Methods

sbull maintains a list of users to simulate media removal. It is the job of the open and close methods to keep track of the current count of users. 

The open method looks very similar to the char driver. It takes inode and file structure pointers as arguments. When an inode refers to a block device, the field `i_bdev->bd_disk` contains a pointer to the associated gendisk structure. This pointer can be used to get to a driver’s internal data structures for the device. Here is the sbull open method:

```c
static int sbull_open(struct inode *inode, struct file *filp)
{
     struct sbull_dev *dev = inode->i_bdev->bd_disk->private_data;
     
     del_timer_sync(&dev->timer);
     filp->private_data = dev;
     spin_lock(&dev->lock);
     if (! dev->users)
        check_disk_change(inode->i_bdev);
     dev->users++;
     spin_unlock(&dev->lock);
     return 0;
}
```

Once the open method has its device structure pointer, it calls del_timer_sync to remove the media removal timer if there is any active. We do not lock the device spinlock until after the timer has been deleted to avoid deadlock if the timer function runs out before we can delete it. With the device locked, we call a kernel function called check_disk_usage to check whether a media change has happened. The last step of the open method is to increment the user count and return. 

The release method decrements the user count and start the media removal timer:

```c
static int sbull_release(struct inode *inode, struct file *filp)
{
     struct sbull_dev *dev = inode->i_bdev->bd_disk->private_data;
     spin_lock(&dev->lock);
     dev->users--;
     if (!dev->users) {
         dev->timer.expires = jiffies + INVALIDATE_DELAY;
         add_timer(&dev->timer);
     }
     spin_unlock(&dev->lock);
     return 0;
}
```

So who actually opens a block device? Some operations cause it to be opened directly from user space. Things like partitioning a disk, building a filesystem on a partition, or running a filesystem checker. A block driver also sees an open call when a partition is mounted. Since the open file is held by the kernel, the block driver cannot tell the difference between operations from user space and those from kernel space. 

### Supporting Removable Media

The `block_device_operations` structure has two methods to support removable media. If your device is non-removable, don't worry about these operations.

The first method is the `media_changed` method that is called to see whether the media has been changed. It will return a nonzero value if this has happened. In sbull, it queries a flag that has been set if the media removal timer has expired (this is used to simulate a device removal):

```c
int sbull_media_changed(struct gendisk *gd)
{
     struct sbull_dev *dev = gd->private_data;
     return dev->media_change;
}
```

The second method is the revalidate method. It is called after a media change to prepare the driver for any operations required on the new media. After calling revalidate, the kernel will attempt to reread the partition table and start over with the device. In sbull, revalidate simple resets the media_change flag and zeros out the device memory (this simulates the insertion of a blank disk):

```c
int sbull_revalidate(struct gendisk *gd)
{
     struct sbull_dev *dev = gd->private_data;
     
     if (dev->media_change) {
         dev->media_change = 0;
         memset (dev->data, 0, dev->size);
     }
     return 0;
}
```

### The ioctl Method

Block devices can provide an ioctl method to perform device control functions, but modern block drivers may not need to implement very many ioctl methods because higher-level block subsystem code intercepts a number of ioctl commands before your driver ever gets to see them. In sbull, the only ioctl command is a request for the device geometry:

```c
int sbull_ioctl (struct inode *inode, struct file *filp,
 unsigned int cmd, unsigned long arg)
{
     long size;
     struct hd_geometry geo;
     struct sbull_dev *dev = filp->private_data;
     
     switch(cmd) {
         case HDIO_GETGEO:
         /*
         * Get geometry: since we are a virtual device, we have to make
         * up something plausible. So we claim 16 sectors, four heads,
         * and calculate the corresponding number of cylinders. We set the
         * start of data at sector four.
         */
         size = dev->size*(hardsect_size/KERNEL_SECTOR_SIZE);
         geo.cylinders = (size & ~0x3f) >> 6;
         geo.heads = 4;
         geo.sectors = 16;
         geo.start = 4;
         if (copy_to_user((void __user *) arg, &geo, sizeof(geo)))
            return -EFAULT;
         return 0;
     }
     return -ENOTTY; /* unknown command */
}
```

The reason we need to provide geometry information is because certain user-space utilities need to be able to query a device's geometry, like the fdisk tool. This allows drivers to be used with old-school tools.

### Request Processing

The most important function of every block driver is the request function. This is where all the real work happens - everything else is just overhead. The block subsystem in the kernel is very complicated to get max performance out of the system. This means that while it is easy to get a simple request function working, writing a driver with great performance on a complex device is extremely difficult. 

### Introduction to the request Method

Prototype for the request method:

```c
void request(request_queue_t *queue);
```

This function is called when the kernel thinks it is time for your driver to process some reads, writes, or other operations. The function itself does not complete all of the requests on the queue before it returns (it might not complete any of them!). It does need to make a start on those requests and ensure that they are all eventually processed by the driver. 

Every device has a request queue because actual transfers to/from the disk can take place far away from the time that the kernel requests them. This is how sbull made its request queue:

```c
dev->queue = blk_init_queue(sbull_request, &dev->lock);
```

When the queue is created, the request function is immediately associated with it. There is also a spinlock created which is held by the kernel whenever a request function is called. The request function thus runs in an atomic context. Everything the driver needs to know about the request is contained within the structures passed to you via the request queue. 

### A Simple request Method

One request method included in sbull is the sbull_request method. It is an example of the simplest possible method:

```c
static void sbull_request(request_queue_t *q)
{
     struct request *req;
     
     while ((req = elv_next_request(q)) != NULL) {
         struct sbull_dev *dev = req->rq_disk->private_data;
         if (! blk_fs_request(req)) {
              printk (KERN_NOTICE "Skip non-fs request\n");
              end_request(req, 0);
              continue;
         }
     sbull_transfer(dev, req->sector, req->current_nr_sectors,
        req->buffer, rq_data_dir(req));
     end_request(req, 1);
     }
}
```

- elv_next_request obtains the first incomplete request on the queue
  - returns NULL when there are no requests to be processed
  - requests are taken off the queue only when they are complete
- block_fs_request tells us whether we are looking at a filesystem request
  - Just one that moves blocks of data
- If a request is not a filesystem request, we pass it to end_request
- When we dispose of nonfilesystem requests, we pass succeeded as 0 to indicate that we did not successfully complete the request
  - Otherwise, we call sbull_transfer to actually move the data, using a set of fields provided in the request structure

Here are the fields provided in the `request` structure:

- `sector_t sector;`
  - The index of the beginning sector on our device
  - Expressed in 512-byte sectors
- `unsigned long nr_sectors;`
  - The number of 512-byte sectors to be transferred
- `char *buffer;`
  - A pointer to the buffer to or from which the data should be transferred
  - This is a kernel virtual address and can be dereferenced directly by the driver
- `rq_data_dir(struct request *req);`
  - Extracts the direction of the transfer from the request
  - 0 return value denotes a read from the device
  - Nonzero denotes a write

sbull can implement the actual data transfer with a call to memcpy because the data is already in memory. We use sbull_transfer for this, and it handles the scaling of vector sizes and ensures we don't copy beyond the end of our virtual device:

```c
static void sbull_transfer(struct sbull_dev *dev, unsigned long sector,
        unsigned long nsect, char *buffer, int write)
{
     unsigned long offset = sector*KERNEL_SECTOR_SIZE;
     unsigned long nbytes = nsect*KERNEL_SECTOR_SIZE;
     
     if ((offset + nbytes) > dev->size) {
         printk (KERN_NOTICE "Beyond-end write (%ld %ld)\n", offset, nbytes);
         return;
     }
     if (write)
         memcpy(dev->data + offset, buffer, nbytes);
     else
         memcpy(buffer, dev->data + offset, nbytes);
}
```

There it is - we now have a complete and simple ram disk device. It is not realistic for a few reasons:

1. sbull executes requests synchronously, one at a time
    - High-performance disk devices are capable of having numerous requests outstanding at the same time
    - The disk’s onboard controller can then choose to execute them in the optimal order
    - sbull can never have multiple requests being fulfilled at a given time
    - In reality, you need to be able to work with more than one request!
2. sbull only transfers one buffer at a time
    - This means that the largest single transfer is almost never going to exceed the size of a single page
    - A block driver can do much better than this by locating related data contiguously on the disk and transfer as many sectors as possible in a single request
    - This speeds things up a lot, but we have ignored all of it in sbull

### Request Queues

Don't overthink it. a block request queue is a queue of block I/O requests. Under the hood, the request queue is a very complex structure, but we don't have to worry about this for most drivers. 

Request queues keep track of outstanding block I/O requests and are important in the creation of those requests. The request queue stores parameters that describe what kinds of requests the device is able to service. These are:

- maximum size
- how many separate segments may go into a request
- hardware sector size
- alignment requirements
- and many others

You can properly configure your request queue such that you will never have a request that your device cannot handle. Request queues also implement a plug-in interface that allows the use of multiple I/O schedulers to be used. The I/O scheduler presents I/O requests to your driver in a way that maximizes performance. There are many schedulers that you can use, including:

- NOOP - acts like a FIFO queue
- Anticipatory - anticipates subsequent block requests and implements request merging, a one-way elevator (a simple elevator), and read and write request batching (this used to be the default in Linux Kernel 2.6)
- Deadline - guarantees a start time for servicing an I/O request by combining request merging, a one-way elevator, and a deadline on all requests
- Completely Fair Queue (CFQ) - uses both request merging and elevators and is a bit more complex that the NOOP or deadline schedulers (this is the current default for Linux)

[This article](https://www.admin-magazine.com/HPC/Articles/Linux-I-O-Schedulers) has a lot more detail on each I/O scheduler and additional links for more info. 

Request queues have type `struct request_queue` or `request_queue_t`. These are defined in `<linux/blkdev.h>`. You can find more details in this header file.

### Queue Creation and Deletion

A request queue is a dynamic data structure that must be created by the block I/O subsystem. Initialize a request queue with:

```c
request_queue_t *blk_init_queue(request_fn_proc *request, spinlock_t *lock);
```

- The arguments are the request function for this queue and a spinlock that controls access to the queue
- This function allocates a good amount of memory (also check to make sure it succeeds)
- You can also set the field `queuedata` to any value
  - It is like the `private_data` field in other structures

To return a request queue to the system, call blk_cleanup_queue like this:

```c
void blk_cleanup_queue(request_queue_t *);
```

### Queueing Functions

There is a very small set of functions for the manipulation of requests on queues. You must hold the lock before calling these functions. One function returns the next request to process. It is called elv_next_request:

```c
struct request *elv_next_request(request_queue_t *queue);
```

- Returns a pointer to the next request to process determined by the I/O scheduler
- Returns NULL if no more requests remain to be processed
- Leaves the request on the queue but marks it as being active
  - This prevents the I/O scheduler from attempting to merge other requests with this one once you start to execute it

To remove a request from the queue, use blkdev_dequeue_request:

```c
void blkdev_dequeue_request(struct request *req);
```

If you need to put a a dequeued request back on the queue (make up your mind, already!), use:

```c
void elv_requeue_request(request_queue_t *queue, struct request *req);
```

### Queue Control Functions

The block layer exports a set of functions that can be used by a driver to control how a request queue operates:

- `void blk_stop_queue(request_queue_t *queue);` and
- `void blk_start_queue(request_queue_t *queue);`
  - If your device has reached a state where it can handle no more outstanding commands, you can call blk_stop_queue to tell the block layer
  - After this call, your request function will not be called until you call blk_start_queue
  - The queue lock must be held when calling either of these functions
- `void blk_queue_bounce_limit(request_queue_t *queue, u64 dma_addr);`
  - Function that tells the kernel the highest physical address to which your device can perform DMA
  - If a request comes in containing a reference to memory above the limit, a bounce buffer (SLOW!) will be used for the operation
  - Provide any reasonable physical address in this argument, or use:
    - BLK_BOUNCE_HIGH - use bounce buffers for high-memory pages (DEFAULT)
    - BLK_BOUNCE_ISA - the driver can DMAonly into the 16-MB ISA zone
    - BLK_BOUNCE_ANY - the driver can perform DMAto any address
- `void blk_queue_max_sectors(request_queue_t *queue, unsigned short max);` and
- `void blk_queue_max_phys_segments(request_queue_t *queue, unsigned short max);` and
- `void blk_queue_max_hw_segments(request_queue_t *queue, unsigned short max);` and
- `void blk_queue_max_segment_size(request_queue_t *queue, unsigned int max);`
  - Functions that set parameters describing the requests that can be satisfied by this device
  - blk_queue_max_sectors can be used to set the maximum size of any request in 512-byte sectors (default is 255)
  - blk_queue_max_phys_segments and blk_queue_max_hw_segments both control how many physical segments (nonadjacent areas in system memory) may be contained within a single request
  - blk_queue_max_phys_segments specifies how many segments your driver is prepared to cope with (Default 128)
  - blk_queue_max_hw_segments is the maximum number of segments that the device itself can handle (Default 128)
  - blk_queue_max_segment_size tells the kernel how large any individual segment of a request can be in bytes (Default is 65,536 bytes)
- `blk_queue_segment_boundary(request_queue_t *queue, unsigned long mask);`
  - Tells the kernel that your device cannot handle requests that cross a particular size memory boundary
- `void blk_queue_dma_alignment(request_queue_t *queue, int mask);`
  - Function that tells the kernel about the memory alignment constraints your device imposes on DMA transfers
  - The default mask is 0x1ff, which causes all requests to be aligned on 512-byte boundaries
- `void blk_queue_hardsect_size(request_queue_t *queue, unsigned short max);`
  - Tells the kernel about your device’s hardware sector size
  - All requests generated by the kernel are a multiple of this size and are properly aligned
  - Remember though: All communications between the block layer and the driver continues to be expressed in 512-byte sectors

### The Anatomy of a Request

We will now dive into how block I/O requests are represented. Each request structure represents one block I/O request (it could have been a merger of several smaller requests made at a higher level). The request is represented as a set of segments, each of which corresponds to one in-memory buffer. The kernel may join multiple requests that involve adjacent sectors on the disk, but it never combines read and write operations within a single request structure. The request structure is implemented as a linked list of bio structures combined with housekeeping info to enable the driver to keep track of its position. 

The bio structure is a low-level description of a portion of a block I/O request. More info below:

#### The bio Structure

When the kernel decides that blocks must be transferrered to/from a block I/O device, it puts together a 'bio' structure to describe the operation. The structure is handed to the block I/O code which merges it into an existing request structure or creates a new one if needed. The 'bio' structure contains all the info that a block driver needs to carry out the request without reference to the user-space process that requested it. 

The 'bio' structure contains the following useful fields:

- `sector_t bi_sector;`
  - The first (512-byte) sector to be transferred for this bio
- `unsigned int bi_size;`
  - The size of the data to be transferred (in bytes)
  - Instead, it is often easier to use bio_sectors(bio), a macro that gives the size in sectors
- `unsigned long bi_flags;`
  - A set of flags describing the bio
  - The least significant bit is set if this is a write
  - The macro bio_data_dir(bio) should be used instead of looking at the flags directly
- `unsigned short bio_phys_segments;` and `unsigned short bio_hw_segments;`
  - The number of physical segments contained within this BIO and the number of segments seen by the hardware after DMA mapping is done
- The core of a bio is an array called bi_io_vec, which is made up of the following structure:

```c
struct bio_vec {
     struct page     *bv_page;
     unsigned        int bv_len;
     unsigned        int bv_offset;
};
```

For a good article on the bio structure relevant to the rest of the system, see this LWN article by Neil Brown:
[A block layer introduction part 1: the bio layer](https://lwn.net/Articles/736534/)

Don't work directly with the bi_io_vec array - you will probably break many things. A set of macros has been provided to ease the process of working with the bio structure. One is called bio_for_each_segment; it loops through every unprocessed entry in the bi_io_vec array. Use this macro like:

```c
int segno;
struct bio_vec *bvec;

bio_for_each_segment(bvec, bio, segno) {
    // Do something with this segment
}
```

- Within this loop, bvec points to the current bio_vec entry
- segno is the current segment number
- These values can be used to set up DMA transfers

If you need to access the pages directly, you should first ensure that a proper kernel virtual address exists with:

```c
char *__bio_kmap_atomic(struct bio *bio, int i, enum km_type type);
void __bio_kunmap_atomic(char *buffer, enum km_type type);
```

- This low-level function allows you to directly map the buffer found in a given bio_vec, as indicated by the index i
- An atomic kmap is created and the caller must provide the appropriate slot to use

The block layer also maintains a set of pointers within the bio structure to keep track of the current state of request processing. Some of the important macros that provide access to this state:

- `struct page *bio_page(struct bio *bio);`
  - Returns a pointer to the page structure representing the page to be transferred next
- `int bio_offset(struct bio *bio);`
  - Returns the offset within the page for the data to be transferred
- `int bio_cur_sectors(struct bio *bio);`
  - Returns the number of sectors to be transferred out of the current page
- `char *bio_data(struct bio *bio);`
  - Returns a kernel logical address pointing to the data to be transferred
  - This does not work for stuff located in high memory
- `char *bio_kmap_irq(struct bio *bio, unsigned long *flags);` and
- `void bio_kunmap_irq(char *buffer, unsigned long *flags);`
  - bio_kmap_irq returns a kernel virtual address for any buffer
    - This is regardless of whether it resides in high or low memory
  - Use bio_kunmap_irq to unmap the buffer
  - `flags` argument is passed by pointer here
  - Since an atomic kmap is used, you cannot map more than one segment at a time

All of the above access the current buffer, which is the first buffer that has not been transferred. Since drivers often want to work through several buffers in the bio before signaling completion on any of them, these functions are often not useful. More macros exist for working with the internals of the bio structure. 

#### Request Structure Fields

We now dive into `struct request` to see how request processing works. Fields of the structure:

- `sector_t hard_sector;` and 
- `unsigned long hard_nr_sectors;` and 
- `unsigned int hard_cur_sectors;`
  - Fields that track the sectors that the driver has yet to complete
  - The first sector that has not been transferred is stored in hard_sector
  - Total number of sectors yet to transfer is in hard_nr_sectors
  - Number of sectors remaining in the current bio is hard_cur_sectors
  - These fields are intended for use only within the block subsystem
    - drivers should not make use of them
- `struct bio *bio;`
  - bio is the linked list of bio structures for this request
  - You should not access this field directly 
    - Use rq_for_each_bio instead
- `char *buffer;`
  - The simple driver example earlier in this chapter used this field to find the buffer for the transfer
  - This field is simply the result of calling bio_data on the current bio
- `unsigned short nr_phys_segments;`
  - The number of distinct segments occupied by this request in physical memory after adjacent pages have been merged
- `struct list_head queuelist;`
  - The linked-list structure that links the request into the request queue
  - If you remove the request from the queue with blkdev_dequeue_request, you may use this list head to track the request in an internal list maintained by your driver

#### Barrier Requests

There is a problem with unrestricted reordering of requests. Some applications require guarantees that certain operations will complete before others are started. The kernel addresses this with the concept of a barrier request. If a request is marked with the `REQ_HARDBARRER` flag, it must be written to the drive before any following request is initiated. 

If your driver honors barrier requests, inform the block layer. Barrier handling is another of the request queues that can be set with:

```c
void blk_queue_ordered(request_queue_t *queue, int flag);
```

- To indicate that your driver implements barrier requests, set the flag parameter to a nonzero value

The actual implementation of barrier requests is done by testing for the associated flag in the request structure. A macro that does this:

```c
int blk_barrier_rq(struct request *req);
```

- Returns a nonzero value if the request is a barrier request

#### Nonretryable Requests

Block drivers often attempt to retry requests that fail the first time to increase reliability and avoid data loss. If your driver is considering retrying a failed request, it should first make this call:

```c
int blk_noretry_request(struct request *req);
```

- If it returns a nonzero value, your driver should simply abort the request
  - The request is not retryable

### Request Completion Functions

When your device has completed transferring some or all of the sectors in an I/O
request, it must inform the block subsystem with:

```c
int end_that_request_first(struct request *req, int success, int count);
```

- This tells the block code that your driver has finished with the transfer of `count` sectors starting where you last left off
- If the I/O was successful, pass `success` as 1; otherwise make it 0
- You must signal completion in order from the first sector to the last
- After completion, you must dequeue the request with blkdev_dequeue_request and pass it to:

```c
void end_that_request_last(struct request *req);
```

- This informs whoever is waiting for the request that it has completed and recycles the request structure
- Must be called with the queue lock held!

Here is one example of doing this:

```c
void end_request(struct request *req, int uptodate)
{
     if (!end_that_request_first(req, uptodate, req->hard_cur_sectors)) {
         add_disk_randomness(req->rq_disk);
         blkdev_dequeue_request(req);
         end_that_request_last(req);
     }
}
```

### Working with bios

If the sbull driver is loaded with the request_mode parameter set to 1, it registers a bio-aware request function instead of the simple function shown earlier. The bio-aware function looks like:

```c
static void sbull_full_request(request_queue_t *q)
{
     struct request *req;
     int sectors_xferred;
     struct sbull_dev *dev = q->queuedata;
     
     while ((req = elv_next_request(q)) != NULL) {
         if (! blk_fs_request(req)) {
             printk (KERN_NOTICE "Skip non-fs request\n");
             end_request(req, 0);
             continue;
         }
         sectors_xferred = sbull_xfer_request(dev, req);
         if (! end_that_request_first(req, 1, sectors_xferred)) {
             blkdev_dequeue_request(req);
             end_that_request_last(req);
         }
     }
}
```

This function takes each request, passes it to sbull_xfer_request, completes it with end_that_request_first, and end_that_request_last if needed. The job of executing a request falls to sbull_xfer_request:

```c
static int sbull_xfer_request(struct sbull_dev *dev, struct request *req)
{
     struct bio *bio;
     int nsect = 0;
     
     rq_for_each_bio(bio, req) {
        sbull_xfer_bio(dev, bio);
        nsect += bio->bi_size/KERNEL_SECTOR_SIZE;
     }
     return nsect;
}
```

- The macro rq_for_each_bio steps through each bio structure in the request
  - It gives us a pointer that we can pass to sbull_xfer_bio for the transfer

```c
static int sbull_xfer_bio(struct sbull_dev *dev, struct bio *bio)
{
     int i;
     struct bio_vec *bvec;
     sector_t sector = bio->bi_sector;
     
     /* Do each segment independently. */
     bio_for_each_segment(bvec, bio, i) {
         char *buffer = __bio_kmap_atomic(bio, i, KM_USER0);
         sbull_transfer(dev, sector, bio_cur_sectors(bio),
             buffer, bio_data_dir(bio) = = WRITE);
         sector += bio_cur_sectors(bio);
         __bio_kunmap_atomic(bio, KM_USER0);
     }
     return 0; /* Always "succeed" */
}
```

- This function steps through each segment in the bio structure, gets a kernel virtual address to access the buffer, and calls the sbull_transfer function to copy the data over

### Block requests and DMA

High performance block drivers will most likely use DMA for the actual data transfers. The easiest way is if your device supports scatter/gather I/O:

```c
int blk_rq_map_sg(request_queue_t *queue, struct request *req,
                  struct scatterlist *list);
```

- This function fills in the given `list` with the full set of segments from the given request
- The return value is the number of entries in the list
- The function also passes back, in its third argument, a scatterlist suitable for passing to dma_map_sg
- You must allocate the storage for a scatterlist before calling blk_rq_map_sg

### Doing Without a Request Queue

Many block-oriented devices like flash memory arrays, readers for media cards, and RAM disks have truly random-access performance and do not benefit from advanced request queueing logic. For these devices, it would be better to accept requests directly from the block layer and not bother with the request queue at all.

The good news is that the  block layer supports a no queue mode of operation. To use this mode, your driver must provide a make request function rather than a request function. make_request has the prototype:

```c
typedef int (make_request_fn) (request_queue_t *q, struct bio *bio);
```

- The request queue is still present, but it will never hold any requests
- make_request can do one of two things:
  - perform the transfer directly
  - redirect the request to another device

If sbull is loaded with request_mode=2, it operates with a make_request function. sbull does this with:

```c
static int sbull_make_request(request_queue_t *q, struct bio *bio)
{
     struct sbull_dev *dev = q->queuedata;
     int status;
     
     status = sbull_xfer_bio(dev, bio);
     bio_endio(bio, bio->bi_size, status);
     return 0;
}
```

And the sbull code to set up the make_request function looks like:

```c
dev->queue = blk_alloc_queue(GFP_KERNEL);
if (dev->queue = = NULL)
    goto out_vfree;
blk_queue_make_request(dev->queue, sbull_make_request);
```

That covers the basics of writing a simple block driver!