// Sample Ramdisk for Kernel Versions 5.3+
// Based off the example provided by CodeImp <https://github.com/CodeImp>

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/blk_types.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <uapi/linux/hdreg.h> //for struct hd_geometry
#include <uapi/linux/cdrom.h> //for CDROM_GET_CAPABILITY

#ifndef SUCCESS
#define SUCCESS 0
#endif

//This defines are available in blkdev.h from kernel 4.17 (vanilla).
#ifndef SECTOR_SHIFT 
#define SECTOR_SHIFT 9
#endif
#ifndef SECTOR_SIZE
#define SECTOR_SIZE (1 << SECTOR_SHIFT)
#endif

// constants - instead defines
static const char* _device_name = "DomsBlockDevice";
static const size_t _buffer_size = 16 * PAGE_SIZE;

// types
typedef struct DomsBlockDevice_cmd_s {
} DomsBlockDevice_cmd_t;

// The internal representation of our device
typedef struct DomsBlockDevice_device_s {
    sector_t capacity; // Device size in bytes
    u8* data;			    // The data aray. u8 - 8 bytes
    atomic_t open_counter;// How many openers

    struct blk_mq_tag_set tag_set;
    struct request_queue *queue;	// For mutual exclusion

    struct gendisk *disk;		// The gendisk structure
} DomsBlockDevice_device_t;

// global variables 

static int _DomsBlockDevice_major = 0;
static DomsBlockDevice_device_t* _DomsBlockDevice_device = NULL;


// functions
static int DomsBlockDevice_allocate_buffer(DomsBlockDevice_device_t* dev)
{
    dev->capacity = _buffer_size >> SECTOR_SHIFT;
    dev->data = kmalloc(dev->capacity << SECTOR_SHIFT, GFP_KERNEL); //
    if (dev->data == NULL) {
        printk(KERN_WARNING "DomsBlockDevice: vmalloc failure.\n");
        return -ENOMEM;
    }

    return SUCCESS;
}

static void DomsBlockDevice_free_buffer(DomsBlockDevice_device_t* dev)
{
    if (dev->data) {
        kfree(dev->data);

        dev->data = NULL;
        dev->capacity = 0;
    }
}

static void DomsBlockDevice_remove_device(void)
{
    DomsBlockDevice_device_t* dev = _DomsBlockDevice_device;
    if (dev == NULL)
        return;

    if (dev->disk)
        del_gendisk(dev->disk);

    if (dev->queue) {
        blk_cleanup_queue(dev->queue);
        dev->queue = NULL;
    }

    if (dev->tag_set.tags)
        blk_mq_free_tag_set(&dev->tag_set);

    if (dev->disk) {
        put_disk(dev->disk);
        dev->disk = NULL;
    }

    DomsBlockDevice_free_buffer(dev);

    kfree(dev);
    _DomsBlockDevice_device = NULL;

    printk(KERN_WARNING "DomsBlockDevice: simple block device was removed\n");
}

static int do_simple_request(struct request *rq, unsigned int *nr_bytes)
{
    int ret = SUCCESS;
    struct bio_vec bvec;
    struct req_iterator iter;
    DomsBlockDevice_device_t *dev = rq->q->queuedata;
    loff_t pos = blk_rq_pos(rq) << SECTOR_SHIFT;
    loff_t dev_size = (loff_t)(dev->capacity << SECTOR_SHIFT);

    printk(KERN_WARNING "DomsBlockDevice: request start from sector %lld \n", blk_rq_pos(rq));
    
    rq_for_each_segment(bvec, rq, iter)
    {
        unsigned long b_len = bvec.bv_len;

        void* b_buf = page_address(bvec.bv_page) + bvec.bv_offset;

        if ((pos + b_len) > dev_size)
            b_len = (unsigned long)(dev_size - pos);

        if (rq_data_dir(rq))//WRITE
            memcpy(dev->data + pos, b_buf, b_len);
        else//READ
            memcpy(b_buf, dev->data + pos, b_len);

        pos += b_len;
        *nr_bytes += b_len;
    }

    return ret;
}

static blk_status_t _queue_rq(struct blk_mq_hw_ctx *hctx, const struct blk_mq_queue_data* bd)
{
    unsigned int nr_bytes = 0;
    blk_status_t status = BLK_STS_OK;
    struct request *rq = bd->rq;

    //we cannot use any locks that make the thread sleep
    blk_mq_start_request(rq);

    if (do_simple_request(rq, &nr_bytes) != SUCCESS)
        status = BLK_STS_IOERR;

    printk(KERN_WARNING "DomsBlockDevice: request process %d bytes\n", nr_bytes);

#if 0 //simply and can be called from proprietary module 
    blk_mq_end_request(rq, status);
#else //can set real processed bytes count 
    if (blk_update_request(rq, status, nr_bytes)) //GPL-only symbol
        BUG();
    __blk_mq_end_request(rq, status);
#endif

    return BLK_STS_OK;//always return ok
}

static struct blk_mq_ops _mq_ops = {
    .queue_rq = _queue_rq,
};

static int _open(struct block_device *bdev, fmode_t mode)
{
    DomsBlockDevice_device_t* dev = bdev->bd_disk->private_data;
    if (dev == NULL) {
        printk(KERN_WARNING "DomsBlockDevice: invalid disk private_data\n");
        return -ENXIO;
    }

    atomic_inc(&dev->open_counter);

    printk(KERN_WARNING "DomsBlockDevice: device was opened\n");

    return SUCCESS;
}
static void _release(struct gendisk *disk, fmode_t mode)
{
    DomsBlockDevice_device_t* dev = disk->private_data;
    if (dev) {
        atomic_dec(&dev->open_counter);

        printk(KERN_WARNING "DomsBlockDevice: device was closed\n");
    }
    else
        printk(KERN_WARNING "DomsBlockDevice: invalid disk private_data\n");
}

static int _getgeo(DomsBlockDevice_device_t* dev, struct hd_geometry* geo)
{
    sector_t quotient;

    geo->start = 0;
    if (dev->capacity > 63) {

        geo->sectors = 63;
        quotient = (dev->capacity + (63 - 1)) / 63;

        if (quotient > 255) {
            geo->heads = 255;
            geo->cylinders = (unsigned short)((quotient + (255 - 1)) / 255);
        }
        else {
            geo->heads = (unsigned char)quotient;
            geo->cylinders = 1;
        }
    }
    else {
        geo->sectors = (unsigned char)dev->capacity;
        geo->cylinders = 1;
        geo->heads = 1;
    }
    return SUCCESS;
}

static int _ioctl(struct block_device *bdev, fmode_t mode, unsigned int cmd, unsigned long arg)
{
    int ret = -ENOTTY;
    DomsBlockDevice_device_t* dev = bdev->bd_disk->private_data;

    printk(KERN_WARNING "DomsBlockDevice: ioctl %x received\n", cmd);

    switch (cmd) {
        case HDIO_GETGEO:
        {
            struct hd_geometry geo;

            ret = _getgeo(dev, &geo );
            if (copy_to_user((void *)arg, &geo, sizeof(struct hd_geometry)))
                ret = -EFAULT;
            else
                ret = SUCCESS;
            break;
        }
        case CDROM_GET_CAPABILITY: //0x5331  / * get capabilities * / 
        {
            struct gendisk *disk = bdev->bd_disk;

            if (bdev->bd_disk && (disk->flags & GENHD_FL_CD))
                ret = SUCCESS;
            else
                ret = -EINVAL;
            break;
        }
    }

    return ret;
}
#ifdef CONFIG_COMPAT
static int _compat_ioctl(struct block_device *bdev, fmode_t mode, unsigned int cmd, unsigned long arg)
{
    // CONFIG_COMPAT is to allow running 32-bit userspace code on a 64-bit kernel
    return -ENOTTY; // not supported
}
#endif

static const struct block_device_operations _fops = {
    .owner = THIS_MODULE,
    .open = _open,
    .release = _release,
    .ioctl = _ioctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl = _compat_ioctl,
#endif
};

//
static int DomsBlockDevice_add_device(void)
{
    int ret = SUCCESS;

    DomsBlockDevice_device_t* dev = kzalloc(sizeof(DomsBlockDevice_device_t), GFP_KERNEL);
    if (dev == NULL) {
        printk(KERN_WARNING "DomsBlockDevice: unable to allocate %ld bytes\n", sizeof(DomsBlockDevice_device_t));
        return -ENOMEM;
    }
    _DomsBlockDevice_device = dev;

    do{
        ret = DomsBlockDevice_allocate_buffer(dev);
        if(ret)
            break;          

        {//configure tag_set
            dev->tag_set.ops = &_mq_ops;
            dev->tag_set.nr_hw_queues = 1;
            dev->tag_set.queue_depth = 128;
            dev->tag_set.numa_node = NUMA_NO_NODE;
            dev->tag_set.cmd_size = sizeof(DomsBlockDevice_cmd_t);
            dev->tag_set.flags = BLK_MQ_F_SHOULD_MERGE;
            dev->tag_set.driver_data = dev;

            ret = blk_mq_alloc_tag_set(&dev->tag_set);
            if (ret) {
                printk(KERN_WARNING "DomsBlockDevice: unable to allocate tag set\n");
                break;
            }
        }

        {//configure queue
            struct request_queue *queue = blk_mq_init_queue(&dev->tag_set);
            if (IS_ERR(queue)) {
                ret = PTR_ERR(queue);
                printk(KERN_WARNING "DomsBlockDevice: Failed to allocate queue\n");
                break;
            }
            dev->queue = queue;
        }
        dev->queue->queuedata = dev;

        {// configure disk
            struct gendisk *disk = alloc_disk(1); //only one partition 
            if (disk == NULL) {
                printk(KERN_WARNING "DomsBlockDevice: Failed to allocate disk\n");
                ret = -ENOMEM;
                break;
            }

            disk->flags |= GENHD_FL_NO_PART_SCAN; //only one partition 
            disk->flags |= GENHD_FL_REMOVABLE;
            disk->major = _DomsBlockDevice_major;
            disk->first_minor = 0;
            disk->fops = &_fops;
            disk->private_data = dev;
            disk->queue = dev->queue;
            sprintf(disk->disk_name, "DomsBlockDevice-%d", 0);
            set_capacity(disk, dev->capacity);

            dev->disk = disk;
            add_disk(disk);
        }

        printk(KERN_WARNING "DomsBlockDevice: simple block device was created\n");
    }while(false);

    if (ret){
        DomsBlockDevice_remove_device();
        printk(KERN_WARNING "DomsBlockDevice: Failed add block device\n");
    }

    return ret;
}

static int __init DomsBlockDevice_init(void)
{
    int ret = SUCCESS;

    _DomsBlockDevice_major = register_blkdev(_DomsBlockDevice_major, _device_name);
    if (_DomsBlockDevice_major <= 0){
        printk(KERN_WARNING "DomsBlockDevice: unable to get major number\n");
        return -EBUSY;
    }

    ret = DomsBlockDevice_add_device();
    if (ret)
        unregister_blkdev(_DomsBlockDevice_major, _device_name);
        
    return ret;
}

static void __exit DomsBlockDevice_exit(void)
{
    DomsBlockDevice_remove_device();

    if (_DomsBlockDevice_major > 0)
        unregister_blkdev(_DomsBlockDevice_major, _device_name);
}

module_init(DomsBlockDevice_init);
module_exit(DomsBlockDevice_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("DOM");
