# LDD3 Chapter 14 Notes

## Chapter 14: The Linux Device Model

Chapter goal: Learn how devices are organized in the kernel. 

Kernel 2.6 provided the abstraction for a unified device model for the kernel as follows:

- Power management and system shutdown
  - These require an understanding of the system’s structure. For example, a USB host adaptor cannot be shut down before dealing with all of the devices connected to that adaptor. The device model enables a traversal of the system’s hardware in the right order.
- Communications with user space
  - The implementation of the sysfs virtual filesystem is tightly tied into the device model and exposes the structure represented by it. The provision of information about the system to user space and knobs for changing operating parameters is increasingly done through sysfs and, therefore, through the device model.
- Hotpluggable devices
  - Peripherals can come and go at the whim of the user. The hotplug mechanism used within the kernel to handle and communicate with user space about the plugging and unplugging of devices is managed through the device model.
- Device classes
  - Many parts of the system don't care how devices are connected, but they need to know what kinds of devices are available. The device model includes a mechanism for assigning devices to classes, which describe those devices at a higher, functional level and allow them to be discovered from user space.
- Object lifecycles
  - Many of the functions described above, including hotplug support and sysfs, complicate the creation and manipulation of objects created within the kernel. The implementation of the device model required the creation of a set of mechanisms for dealing with object lifecycles, their relationships to each other, and their representation in user space.

A full device model is a very complicated and huge tree of nodes. The Linux device model code takes care of all these considerations to simplify things with the driver authors. Usually we can trust the device model to take care of itself. There are times when it is nice or important to know the device model, though, so we will go over that in this chapter's notes. This chapter goes into a lot of nitty-gritty, low-level details.

### Kobjects, Ksets, and Subsystems

kobject is the fundamental structure holding the device model together. Tasks handled by `struct kobject` include:

- Reference counting of objects
  - Often, when a kernel object is created,there is no way to know just how long it will exist. One way of tracking the lifecycle of such objects is through reference counting. When no code in the kernel holds a reference to a given object, that object has finished its useful life and can be deleted.
- Sysfs representation
  - Every object that shows up in sysfs has a kobject that interacts with the kernel to create its visible representation.
- Data structure glue
  - The device model is a freakishly complicated data structure made up of multiple hierarchies with numerous links between them. The kobject implements this structure and holds it together. (Thanks, Linus :thumbsup:)
- Hotplug event handling (should I stay or should I go, now)
  - The kobject subsystem handles the generation of events that notify user space about the comings and goings of hardware on the system. You gotta know when to say goodbye. 

Let's break down the kobject into smaller chunks to try and understand it better. 

### Kobject Basics

A kobject has type `struct kobject` defined in `<linux/kobject.h>`. Let's first look at the embedding of kobjects:

The functions handled by kobjects are pretty much all services performed on behalf of other objects. A kobject doesn't do/mean much as an independent unit; it exists to tie a higher-level object into the device model. It is really rare for kernel code to create a standalone kobject. <b>Kobjects are used to control access to a larger, domain-specific object.</b> Kobjects are found embedded in other structures. Kobjects can be seen as a top-level, abstract class that other classes are derived from. One example: cdev structure

```c
struct cdev {
     struct kobject kobj; //HERE IT IS!
     struct module *owner;
     struct file_operations *ops;
     struct list_head list;
     dev_t dev;
     unsigned int count;
};
```

And the proper way to convert a pointer to a `struct kobject` called `kp` embedded in a `struct cdev` would be:

```c
struct cdev *device = container_of(kp, struct cdev, kobj);
```

Initialization of a kobject is not super simple. First, the entire kobject is set to 0 with a call to memset. Failure to do this leads to strange crashes. Next, set up some of the internal fields with a call to kobject_init() 

```c
void kobject_init(struct kobject *kobj);
```

This sets the kobjects reference count to 1 (so it doesn't get removed right away) and does some other things. You must also set the name of the kobject used in sysfs entries. To do this, use:

```c
int kobject_set_name(struct kobject *kobj, const char *format, ...);
```

Other fields that need to be set are ktype, kset, and parent. We will get to these later. 

Low-level functions for manipulating a kobject's reference counts are:

```c
struct kobject *kobject_get(struct kobject *kobj);
void kobject_put(struct kobject *kobj);
```

A successful call to kobject_get increments the kobject’s reference counter and returns a pointer to the kobject. If, however, the kobject is already in the process of being destroyed, the operation fails and kobject_get returns NULL. This return value must always be tested, or no end of unpleasant race conditions could result. A call to kobject_put decrements the reference count and could potentially free the object.

In some cases the reference count in the kobject itself may not be sufficient to prevent race conditions. We don't want to unload the module while that kobject is being passed around. As a result, structures like `cdev` contain a `struct module` pointer that looks like this:

```c
struct kobject *cdev_get(struct cdev *p)
{
     struct module *owner = p->owner;
     struct kobject *kobj;
     if (owner && !try_module_get(owner))
        return NULL;
     kobj = kobject_get(&p->kobj);
     if (!kobj)
        module_put(owner);
     return kobj;
}
```

Creating a reference to a `cdev` structure requires creating an additional reference to the module that owns it. `cdev_get` uses `try_module_get` to attempt to increment that module’s usage count. On success, it increments the reference count with `kobject_get`. On failure, check the return value and release the reference to the module. 

Now let's talk about the death of a kobject. Sometimes the reference count reaches 0 (oh no!). This is a sign that the object should be released. We do this with the `release` method as follows:

```c
void my_object_release(struct kobject *kobj)
{
    struct my_object *mine = container_of(kobj, struct my_object, kobj);
    
    /* Perform any additional cleanup on this object, then... */
    kfree(mine);
}
```

- EVERY KOBJECT MUST HAVE A RELEASE METHOD!

The release method is not stored in the kobject itself but with the strucutre that contains the kobject. This bigger structure is called a `kobj_type` and commonly referred to as the ktype. Here is the structure:

```c
struct kobj_type {
     void (*release)(struct kobject *);
     struct sysfs_ops *sysfs_ops;
     struct attribute **default_attrs;
};
```

The release field is the one we just talked about. We will discuss the other two later in this chapter. Every kobject needs to have a `kobj_type` structure. The pointer to this structure can be found in two places:

1. The kobject structure itself contains a field (called ktype) that can contain this pointer
2. If this kobject is a member of a kset, the `kobj_type` pointer is provided by that kset instead

The following macro finds the `kobj_type` pointer for a given kobject:

```c
struct kobj_type *get_ktype(struct kobject *kobj);
```

### Kobject Hierarchies, Ksets, and Subsystems

The kobject structure is often used to link together objects into a hierarchical structure similar to the system being modeled. The two mechanisms we can use for linking are called the parent pointer and ksets. 

The parent field in `struct kobject` is a pointer to another kobject. The one it points to is higher up in the overall system hierarchy. Example: a USB driver may have the hub as the parent. The main use for the parent pointer is to position the object correctly in the sysfs hierarchy. 

Ksets are collections of kobjects embedded within strucutures of the same type. We use these to contain - it acts as a top-level container class for kobjects. To use ksets, the kobject’s kset field must first be pointed at the kset of interest. Then, the kobject should be passed to: 

```c
int kobject_add(struct kobject *kobj);
```

There is a convenience function that combines kobject_init and kobject_add into one function. It can be used with:

```c
extern int kobject_register(struct kobject *kobj);
```

To remove a kobject from the kset, use:

```c
void kobject_del(struct kobject *kobj);
```

Note: there is also a kobject_unregister function that is a combination of kobject_del and kobject_put.

So why use kets? Ksets keeps children in a standard kernel linked list. They have a similar initialization and setup as kobjects, as well as a name. A subsystem within Linux is essentially a wrapper around a kset that includes a semaphore. A kset needs to belong to a subsystem. The semaphore in the subsystem is used to serialize access to a kset’s internal-linked list. Don't create new subsystems - you probably don't/shouldn't need to do this for your device driver. 

### Low-Level Sysfs Operations

Kobjects are the mechanism behind the sysfs virtual filesystem. Every directory in sysfs has a kobject associated with it. Every kobject also exports one or more attributes, which appear in that kobject’s sysfs directory as files containing kernel-generated information.

All code that works with sysfs should include `<linux/sysfs.h>`. kobject_add will create a new sysfs entry, but there are a few things worth noting:

- Sysfs entries for kobjects are always directories, so a call to kobject_add results in the creation of a directory in sysfs
- The name assigned to the kobject (with kobject_set_name) is the name used for the sysfs directory
  - must be unique!
  - should be reasonable file names too
- The sysfs entry is located in the directory corresponding to the kobject’s parent pointer
  - Parent can be NULL, it is set to the kobject embedded in the kset
  - If kset and parent are NULL, it adds the object to the top level. DO NOT DO THIS!

### Default Attributes

Every kobject is given a default set of attributes at creation. The default_attrs field lists the attributes to be created. This field points to an array of pointers to attribute structures like this:

```c
struct attribute {
     char *name;
     struct module *owner;
     mode_t mode;
};
```

- `name` is the name of the attribute (as it appears within the kobject’s sysfs directory)
- `owner` is a pointer to the module (if any) that is responsible for the implementation of this attribute
- `mode` is the protection bits that are to be applied to this attribute
  - Usually `S_IRUGO` for read-only attributes but can be made writable for root
- The last entry in `default_attrs` list must be zero filled

To actually implement the attributes, use the `kobj_type->sysfs_ops` field that points to a structure with prototype:

```c
struct sysfs_ops {
     ssize_t (*show)(struct kobject *kobj, struct attribute *attr,
                     char *buffer);
     ssize_t (*store)(struct kobject *kobj, struct attribute *attr,
                      const char *buffer, size_t size);
};
```

- The show method is used for reading an attribute from user space
  - It enocdes the value into a buffer - make sure not to overrun the buffer
- Store is similar but works in reverse. It needs to decode data from a buffer and store and respond to the value in a way that makes sense
  - This can only be called if write access is allowed to the attribute

### Nondefault Attributes

Attributes can be added and removed from objects at will if you really want to do that. To add a new one, fill in a attribute structure and pass it to:

```c
int sysfs_create_file(struct kobject *kobj, struct attribute *attr);
```

And to remove an attribute, call:

```c
int sysfs_remove_file(struct kobject *kobj, struct attribute *attr);
```

- Be careful using this one - a user-space process could still have an open file descriptor for this attribute and the show and store calls are still possible after the attribute has been removed

### Binary Attributes

It is rare that you will need this. If you need attributes to handle larger chuncks of binary data, use the bin_attribute structure:

```c
struct bin_attribute {
     struct attribute attr;
     size_t size;
     ssize_t (*read)(struct kobject *kobj, char *buffer,
                     loff_t pos, size_t size);
     ssize_t (*write)(struct kobject *kobj, char *buffer,
                      loff_t pos, size_t size);
};
```

Look up the man page for how to fill out this stucture - it is very similar to a normal attr. To create a binary attribute, call:

```c
int sysfs_create_bin_file(struct kobject *kobj, struct bin_attribute *attr);
```

And remove it with:

```c
int sysfs_remove_bin_file(struct kobject *kobj, struct bin_attribute *attr);
```

### Symbolic Links

Sysfs has the usual tree structure, but object relationships to the kernel are more complicated. One tree has all devices known to the system, while another has all the device drivers. We want to see how these trees are interacting. Sysfs does this through symbolic links. A symbolic link can be created with:

```c
int sysfs_create_link(struct kobject *kobj, struct kobject *target, char *name);
```

- This function creates a link called `name` pointing to the target's sysfs entry as an attribute of kobj. 

Symbolic links remain after a target has been removed. Be careful with this. Symbolic links can be removed using:

```c
void sysfs_remove_link(struct kobject *kobj, char *name);
```

### Hotplug Event Generation

A hotplug event is a notification to user space from the kernel that something has changed in the system’s configuration. These are generated whenever a kobject is created or destroyed. Hotplug events invoke /sbin/hotplug, which can respond to each event by loading drivers, creating device nodes, mounting partitions, or taking any other action that is appropriate.

### Hotplug Operations

Control of hotplug events is handled in the `kset_hotplug_ops` structure as:

```c
struct kset_hotplug_ops {
     int (*filter)(struct kset *kset, struct kobject *kobj);
     char *(*name)(struct kset *kset, struct kobject *kobj);
     int (*hotplug)(struct kset *kset, struct kobject *kobj,
         char **envp, int num_envp, char *buffer,
         int buffer_size);
};
```

- A pointer to this structure is found in the hotplug_ops field of the kset structure
- If a given kobject is not contained within a kset, the kernel searches up the hierarchy until it finds a kobject that does have a kset
  - This ket's hotplug ops are then used
- The filter hotplug operation is called whenever the kernel is considering generating an event for a given kobject
  - If filter returns 0, the event is not created
  - This gives the kset code ability to decide which events should be passed to user space

The prototype for hotplug is:

```c
int (*hotplug)(struct kset *kset, struct kobject *kobj,
               char **envp, int num_envp, char *buffer,
               int buffer_size);
```

- `kset` and `kobject` describe the object for which the event is being generated
- `envp` array is a place to store additional environment variable definitions
  - It has `num_envp` entries available
  - Variables are encoded into `buffer` of size `buffer_size` bytes
  - NULL should be added to the end of envp

### Buses, Devices, and Drivers

We will now do an example by introducing the lddbus, a virtual bus, and modify scullp to connect to the virtual bus. Most driver writers will never need this level of tinkering - this is really low level bus stuff.

#### Buses

A bus is a channel between the processor and one or more devices. In the device model, all devices are connected via a bus, even if it is an internal, virtual bus. Buses can plug into each other (ex: USB->PCI connection). In the Linux device model, a bus is represented by the `bus_type` structure defined in `<linux/device.h>`. It is defined as:

```c
struct bus_type {
     char *name;
     struct subsystem subsys;
     struct kset drivers;
     struct kset devices;
     int (*match)(struct device *dev, struct device_driver *drv);
     struct device *(*add)(struct device * parent, char * bus_id);
     int (*hotplug) (struct device *dev, char **envp,
                     int num_envp, char *buffer, int buffer_size);
     /* Some fields omitted b/c really big*/
};
```

- `name` is the name of the bus, like pci
- Each bus is its own subsystem under the /bus sysfs system
- A bus contains 2 ksets:
  - One representing all known drivers for that bus
  - One for all devices plugged into the bus

#### Bus Registration

The virtual lddbus sets up its `bus_type` as follows:

```c
struct bus_type ldd_bus_type = {
   .name = "ldd",
   .match = ldd_match,
   .hotplug = ldd_hotplug,
};
```

- We only need to specify the name of the bus and any methods that go along with it

A new bus is registered with the system via a call to bus_register (very good name here lol). lddbus code does it in this way:

```c
ret = bus_register(&ldd_bus_type);
if (ret)
    return ret;
```

You can also remove a bus with a call to bus_unregister:

```c
void bus_unregister(struct bus_type *bus);
```

#### Bus Methods

- `int (*match)(struct device *device, struct device_driver *driver);`
  - This method is called, perhaps multiple times, whenever a new device or driver is added for this bus. It should return a nonzero value if the given device can be handled by the given driver.
- `int (*hotplug) (struct device *device, char **envp, int num_envp, char *buffer, int buffer_size);`
  - This method allows the bus to add variables to the environment prior to the generation of a hotplug event in user space. The parameters are the same as for the kset hotplug method.

lddbus has a simple match function to compare the driver and device names:

```c
static int ldd_match(struct device *dev, struct device_driver *driver)
{
    return !strncmp(dev->bus_id, driver->name, strlen(driver->name));
}
```

With real hardware, the match function usually compares the hardware ID with IDs supported by the driver. The lddbus hotplug method looks like this:

```c
static int ldd_hotplug(struct device *dev, char **envp, int num_envp,
 char *buffer, int buffer_size)
{
     envp[0] = buffer;
     if (snprintf(buffer, buffer_size, "LDDBUS_VERSION=%s",
                  Version) >= buffer_size)
        return -ENOMEM;
     envp[1] = NULL;
     return 0;
}
```

#### Iterating Over Devices and Drivers

To operate on every device in a bus, use:

```c
int bus_for_each_dev(struct bus_type *bus, struct device *start,
                     void *data, int (*fn)(struct device *, void *));
```

-  If start is NULL,the iteration begins with the first device on the bus
    - otherwise iteration starts with the first device after start

There is a similar function for iterating over drivers on a bus:

```c
int bus_for_each_drv(struct bus_type *bus, struct device_driver *start,
                     void *data, int (*fn)(struct device_driver *, void *));
```

- It works the same as the device version
- Both of these will hold the R/W semaphore for the bus subsystem
  - Do not implement additional mutexs on top of this

#### Bus Attributes

You can even add attributes to buses! The `bus_attribute` type is defined in `<linux/device.h>`:

```c
struct bus_attribute {
     struct attribute attr;
     ssize_t (*show)(struct bus_type *bus, char *buf);
     ssize_t (*store)(struct bus_type *bus, const char *buf,
                      size_t count);
};
```

- This should look very similar to the attribute stuff used before

There is a convenience macro for compile-time creation and initialization of bus-attribute structures:

```c
BUS_ATTR(name, mode, show, store);
```

- The generated name from this macro is `bus_attr_"name"`

Any attributes belonging to a bus should be created explicitly with bus_create_file:

```c
int bus_create_file(struct bus_type *bus, struct bus_attribute *attr);
```

These attributes can also be removed with:

```c
void bus_remove_file(struct bus_type *bus, struct bus_attribute *attr);
```

The lddbus driver creates a simple attribute file containing the source version number. The show method of this attribute looks like:

```c
static ssize_t show_bus_version(struct bus_type *bus, char *buf)
{
    return snprintf(buf, PAGE_SIZE, "%s\n", Version);
}

static BUS_ATTR(version, S_IRUGO, show_bus_version, NULL);
```

Creating the attribute file is done at module load time:

```c
if (bus_create_file(&ldd_bus_type, &bus_attr_version))
    printk(KERN_NOTICE "Unable to create version attribute\n");
```

This call creates an attribute file (located at /sys/bus/ldd/version) containing the revision number for the lddbus code.

#### Devices

Every device in the Linux system is eventually represented as an instance of `struct device` at the lowest level:

```c
struct device {
     struct device *parent;
     struct kobject kobj;
     char bus_id[BUS_ID_SIZE];
     struct bus_type *bus;
     struct device_driver *driver;
     void *driver_data;
     void (*release)(struct device *dev);
     /* Several fields omitted b/c big! */
};
```

The fields actually worth knowing about are:

- `struct device *parent`
  - The device’s “parent” device—the device to which it is attached
  - In most cases,a parent device is some sort of bus or host controller
- `struct kobject kobj;`
  - The kobject that represents this device and links it into the hierarchy
  - Note: device->kobj->parent is equal to &device->parent->kobj
- `char bus_id[BUS_ID_SIZE];`
  - A string that uniquely identifies this device on the bus
- `struct bus_type *bus;`
  - Identifies which kind of bus the device sits on
- `struct device_driver *driver;`
  - The driver that manages this device
  - More info on this in the next section
- `void *driver_data;`
  - A private data field that may be used by the device driver
- `void (*release)(struct device *dev);`
  - The method is called when the last reference to the device is removed

At the very minimum, the parent, bus_id, bus,and release fields must be set.

#### Device Registration

Typical (un)registration functions exist as:

```c
int device_register(struct device *dev);
void device_unregister(struct device *dev);
```

- Note: an actual bus IS a device and must be registered seperately

lddbus module supports only a single virtual bus, and the driver sets it up at compile time with:

```c
static void ldd_bus_release(struct device *dev)
{
    printk(KERN_DEBUG "lddbus release\n");
}

struct device ldd_bus = {
   .bus_id = "ldd0",
   .release = ldd_bus_release
};
```

- This is created as a top level bus. There is a simple no-op release method, and as the first bus its name is ldd0. The bus device is registered with:

```c
ret = device_register(&ldd_bus);
if (ret)
    printk(KERN_NOTICE "Unable to register ldd0\n");
```

Once that call is complete, the new bus is visible in /sys/devices in sysfs. Devices added to this bus then show up in /sys/devices/ldd0/.

#### Device Attributes

Even devices can have attributes! What a time to be alive!

These attributes show up in sysfs. The structure to use is:

```c
struct device_attribute {
     struct attribute attr;
     ssize_t (*show)(struct device *dev, char *buf);
     ssize_t (*store)(struct device *dev, const char *buf,
                      size_t count);
};
```

Attribute structures for devices can be setup at compile time with:

```c
DEVICE_ATTR(name, mode, show, store);
```

- The resulting structure is named as `dev_attr_"name"`. 

The management of attribute files is handled with the typical pair of functions"

```c
int device_create_file(struct device *device,
                       struct device_attribute *entry);
void device_remove_file(struct device *dev,
                        struct device_attribute *attr);
```

#### Device Structure Embedding

The `device` structure contains the information that the device model core needs to model the system. Most subsystems will also track additional info about the devices they host. Usually devices need more than just the barebones `device` structures. That structure is usually embedded in a higher-level representation. The lddbus driver creates its own device type (struct ldd_device) and expects individual device drivers to register their devices using that type. Here is the structure:

```c
struct ldd_device {
     char *name;
     struct ldd_driver *driver;
     struct device dev;
};

#define to_ldd_device(dev) container_of(dev, struct ldd_device, dev);
```

The registration interface exported by lddbus looks like this:

```c
int register_ldd_device(struct ldd_device *ldddev)
{
     ldddev->dev.bus = &ldd_bus_type;
     ldddev->dev.parent = &ldd_bus;
     ldddev->dev.release = ldd_dev_release;
     strncpy(ldddev->dev.bus_id, ldddev->name, BUS_ID_SIZE);
     return device_register(&ldddev->dev);
}
EXPORT_SYMBOL(register_ldd_device);
```

To show how this interface is used, look at the sculld driver. It works with the lddbus interface to create a memory area to read/write to. sculld also adds an attribute of its own to its device entry. This is the setup for the attribute:

```c
static ssize_t sculld_show_dev(struct device *ddev, char *buf)
{
     struct sculld_dev *dev = ddev->driver_data;
     
     return print_dev_t(buf, dev->cdev.dev);
}

static DEVICE_ATTR(dev, S_IRUGO, sculld_show_dev, NULL);
```

At initialization time the device is registered and the `dev` attribute is created
through the following function:

```c
static void sculld_register_dev(struct sculld_dev *dev, int index)
{
     sprintf(dev->devname, "sculld%d", index);
     dev->ldev.name = dev->devname;
     dev->ldev.driver = &sculld_driver;
     dev->ldev.dev.driver_data = dev;
     register_ldd_device(&dev->ldev);
     device_create_file(&dev->ldev.dev, &dev_attr_dev);
}
```

- We make use of the `driver_data` field to store the pointer to our own internal device structure

#### Device Drivers (Seems like a small section for being the title of this book ;)) 

The device model tracks all drivers known to the system. This is done so the core can match drivers to new devices. Device drivers can export info and configuration variables that are independent of any specific device. Drivers are defined by the following structure defined in `<linux/device.h>`:

```c
struct device_driver {
     char *name;
     struct bus_type *bus;
     struct kobject kobj;
     struct list_head devices;
     int (*probe)(struct device *dev);
     int (*remove)(struct device *dev);
     void (*shutdown) (struct device *dev);
     /* Some fields omitted b/c really big*/
};
```

- `name` is the name of the driver in sysfs
- `bus` is the type of bus this driver works with
- `kobj` is the kobject
- `devices` is a list of all devices currently bound to this driver
- `probe` is a function called to query the existence of a specific device
- `remove` is called when the device is removed from the system
- `shutdown` is called at shutdown time to quiesce the device

And the (un)registration functions look like the usual:

```c
int driver_register(struct device_driver *drv);
void driver_unregister(struct device_driver *drv);
```

It also has the usual attribute structure:

```c
struct driver_attribute {
     struct attribute attr;
     ssize_t (*show)(struct device_driver *drv, char *buf);
     ssize_t (*store)(struct device_driver *drv, const char *buf,
     size_t count);
};
DRIVER_ATTR(name, mode, show, store);
```

With attribute files created in the same way as before:

```c
int driver_create_file(struct device_driver *drv,
                       struct driver_attribute *attr);
void driver_remove_file(struct device_driver *drv,
                        struct driver_attribute *attr);
```

- The `bus_type` structure contains the field `drv_attrs` that points to a set a default attributes that are created for all drivers on this type of bus

#### Driver Structure Embedding

The `device_driver` structure is typically embedded in other structures - lddbus follows this trend. Here is how it is defined:

```c
struct ldd_driver {
     char *version;
     struct module *module;
     struct device_driver driver;
     struct driver_attribute version_attr;
};

#define to_ldd_driver(drv) container_of(drv, struct ldd_driver, driver);
```

Bus-specific driver registration is done with:

```c
int register_ldd_driver(struct ldd_driver *driver)
{
     int ret;
     
     driver->driver.bus = &ldd_bus_type;
     ret = driver_register(&driver->driver);
     if (ret)
        return ret;
     driver->version_attr.attr.name = "version";
     driver->version_attr.attr.owner = driver->module;
     driver->version_attr.attr.mode = S_IRUGO;
     driver->version_attr.show = show_version;
     driver->version_attr.store = NULL;
     return driver_create_file(&driver->driver, &driver->version_attr);
}
```

First, this function registers the low-level device_driver structure with the core. The rest sets up the version attribute. The owner of the attribute is set to the driver module, not the lddbus module. This revents the module from being unloaded while user-space holds the attribute file open. The show function is:

```c
static ssize_t show_version(struct device_driver *driver, char *buf)
{
     struct ldd_driver *ldriver = to_ldd_driver(driver);
     
     sprintf(buf, "%s\n", ldriver->version);
     return strlen(buf);
}
```

Sculld then created the full ldd driver as:

```c
static struct ldd_driver sculld_driver = {
     .version = "$Revision: 1.1 $",
     .module = THIS_MODULE,
     .driver = {
        .name = "sculld",
     },
};
```

The view from sysfs will then show the driver under /sys/bus/ldd/drivers. Yay! It works!

### Classes (students HATE them! Find out why now!)

Classes are the final device model we will look at. A class is a higher-level view of a device that abstracts out low-level implementation details (sounds nice!). Classes allow user space to work with devices based on what they do, rather than how they are connected or how they work. This sounds too easy...

Almost all classes appear in /sys/class. Here is an output from my machine:

```bash
$ ls /sys/class

ata_device     drm             mdio_bus        ppdev         sound
ata_link       drm_dp_aux_dev  mem             ppp           spi_master
ata_port       extcon          memstick_host   pps           spi_slave
backlight      firmware        misc            printer       thermal
bdi            gpio            mmc_host        ptp           tpm
block          graphics        nd              pwm           tpmrm
bluetooth      hidraw          net             rapidio_port  tty
bsg            hwmon           nvme            regulator     usbmisc
dax            i2c-adapter     nvme-subsystem  rfkill        vc
devcoredump    i2c-dev         pci_bus         rtc           video4linux
devfreq        ieee80211       pci_epc         scsi_device   virtio-ports
devfreq-event  input           phy             scsi_disk     vtconsole
dma            iommu           powercap        scsi_generic  watchdog
dmi            leds            power_supply    scsi_host     wmi_bus
```

Classes can be much easier to work with than hardware-oriented parts of sysfs with nasty names. The driver core has two interfaces for managing classes:

- class_simple routines are designed to make it as easy as possible to add new classes to the system
  - Usually used to expose attributes containing device numbers to enable the automatic creation of device nodes
- The regular class interface is more complex but offers more features as well

#### The class_simple Interface

This interface was meant to be REALLY simple to you could at least see a device's assigned number. So easy a grad student could do it :wink: 

To use this interface, first create the class with a call to class_simple_create:

```c
struct class_simple *class_simple_create(struct module *owner, char *name);
```

- This creates a class with the name `name`. 

It can be destroyed with:

```c
void class_simple_destroy(struct class_simple *cs);
```

To add devices to this class, use:

```c
struct class_device *class_simple_device_add(struct class_simple *cs,
                                             dev_t devnum,
                                             struct device *device,
                                             const char *fmt, ...);
```

- `cs` is the previously created simple class
- `devnum` is the assigned device number
- `device` is the struct device representing this device
- Everything else are a printk-style format string and arguments to create the device name

Classes generate hotplug events when devices come and go. If the driver needs to add variables to the environment for the user-space event handler, it can use a hotplug callback:

```c
int class_simple_set_hotplug(struct class_simple *cs,
                             int (*hotplug)(struct class_device *dev,
                             char **envp, int num_envp,
                             char *buffer, int buffer_size));
```

When the device goes away, remove the class entry with:

```c
void class_simple_device_remove(dev_t dev);
```

#### The Full Class Interface

Sometimes simple is not enough. You need more. MORE. 

A class is defined by an instance of `struct class` as follows:

```c
struct class {
     char *name;
     struct class_attribute *class_attrs;
     struct class_device_attribute *class_dev_attrs;
     int (*hotplug)(struct class_device *dev, char **envp,
                    int num_envp, char *buffer, int buffer_size);
     void (*release)(struct class_device *dev);
     void (*class_release)(struct class *class);
     /* Some fields omitted b/c BIG */
};
```

- Each class needs a unique name
- When class is registered, all attributes listed in `class_attrs` is created

The (un)registration functions are:

```c
int class_register(struct class *cls);
void class_unregister(struct class *cls);
```

And the interface for attributes is the same old, same old:

```c
struct class_attribute {
     struct attribute attr;
     ssize_t (*show)(struct class *cls, char *buf);
     ssize_t (*store)(struct class *cls, const char *buf, size_t count);
};

CLASS_ATTR(name, mode, show, store);

int class_create_file(struct class *cls,
                      const struct class_attribute *attr);
void class_remove_file(struct class *cls,
                       const struct class_attribute *attr);
```

The real purpose of a class is to serve as a container for the devices that are members of that class. A member is represented by a `struct class_device` as follows:

```c
struct class_device {
     struct kobject kobj;
     struct class *class;
     struct device *dev;
     void *class_data;
     char class_id[BUS_ID_SIZE];
};
```

- `class_id` holds the name of this device as it appears in sysfs
- `class` pointer should point to the class holding this device
- `dev` should point to the associated device structure
  - Setting dev is optional

Again, we have the typical (un)register functions:

```c
int class_device_register(struct class_device *cd);
void class_device_unregister(struct class_device *cd);
```

You can even rename an already registered entry with:

```c
int class_device_rename(struct class_device *cd, char *new_name);
```

And attributes look like this:

```c
struct class_device_attribute {
     struct attribute attr;
     ssize_t (*show)(struct class_device *cls, char *buf);
     ssize_t (*store)(struct class_device *cls, const char *buf,
                      size_t count);
};

CLASS_DEVICE_ATTR(name, mode, show, store);

int class_device_create_file(struct class_device *cls,
                             const struct class_device_attribute *attr);
void class_device_remove_file(struct class_device *cls,
                              const struct class_device_attribute *attr);
```

There is a new mechanism to explore with classes called an interface. This is used to get notification when devices enter or leave the class. Prototype:

```c
struct class_interface {
     struct class *class;
     int (*add) (struct class_device *cd);
     void (*remove) (struct class_device *cd);
};
```

Typcial (un)register functions:

```c
int class_interface_register(struct class_interface *intf);
void class_interface_unregister(struct class_interface *intf);
```

The interface mechanism is nice. When a new device joins, the add function is called. When the device is removed, the remove method cleans stuff up nicely. Multiple interfaces can be registered for a class. 

### Hotplug :fire: :electric_plug: :fire:

The kernel and the user view hotplugging differently (just a matter of opinion, no hard feelings). The kernel views it as an interaction between the hardware, the kernel, and the kernel driver. Users view it as an interaction between the kernel and user space through a program called /sbin/hotplug. The program is called by the kernel when it wants to notify the user of an event happening that is important!

#### Dynamic Devices

These are devices that can be connected and disconnected after a computer has been powered on. Users love it. Driver devs probably see it as a huge burden. But, with USB-C and Thunderbolt taking over the world, there will most likely be hotplug devices for the rest of time. How do you handle users ripping out a flashdrive without doing ejecting it? Each bus handles the loss of a device in a different way. 

For USB drivers, when the device that a USB driver is bound to is removed from the
system, any pending urbs that were submitted to the device start failing with the
error -ENODEV. The driver needs to recognize this error and properly clean up any
pending I/O if it occurs.

Many devices can be hotplugged - even CPUs and memory sticks!

#### The /sbin/hotplug Utility

When a hotplug event is created, the kernel calls the user-space program /sbin/hotplug. It is a small script that passes execution on to a list of other programs placed in the /etc/hot-plug.d/ directory tree. (Find hotplug now at /proc/sys/kernel/hotplug). The script typically looks like:

```shell
DIR="/etc/hotplug.d"
for I in "${DIR}/$1/"*.hotplug "${DIR}/"default/*.hotplug ; do
    if [ -f $I ]; then
        test -x $I && $I $1 ;
    fi
done
exit 1
```

It really just searches for all programs with a .hotplug suffix that might be interested in this event and invokes them. The command-line argument passed to /sbin/hotplug is the name associated with the hotplug event as determined by the kset assigned to the kobject. The name is found in the kset’s hotplug_ops structure. If that function is not present or never called, the name is that of the kset itself. The default environment variables are:

- `ACTION`
  - The string add or remove, depending on whether the object in question was just created or destroyed
- `DEVPATH`
  - A directory path within the sysfs filesystem
  - It points to the kobject that is being either created or destroyed
- `SEQNUM`
  - The sequence number for this hotplug event
  - This is a 64 bit number incremented for every hotplug event that is generated
  - Enables user space to sort the hotplug events in the order in which the kernel generates them
  - Avoids race conditions in user space with timing
- `SUBSYSTEM`
  - The same string passed as the command-line argument as described above

Buses will add their own environment variables as well. Look into this when using hotplug events. Two good tools to handle the /sbin/hotplug ultility in user space. They are Linux Hotplug Scripts and udev. 

#### Linux Hotplug Scripts

- These scripts look at the different environment variables that the kernel sets to describe the device that was just discovered and then tries to find a kernel module that matches up with that device
- When a driver uses the MODULE_DEVICE_TABLE macro, depmod takes that information and creates the files located in /lib/module/KERNEL_VERSION/modules.*map
- The hotplug scripts use these module map text files to determine what module to try to load to support the device that was recently discovered by the kernel
- They load all modules and do not stop at the first match
- This lets the kernel work out which module is best

#### udev (This is important for Teensy Microcontrollers!)

- Policy decisions, such as what name to give a device, can be specified in user space, outside of the kernel
- Ensures that the naming policy is removed from the kernel
  - Allows a large amount of flexibility about the name of each device
- All that a device driver needs to do for udev to work properly with it is ensure that any major and minor numbers assigned to a device controlled by the driver are exported to user space through sysfs
- This can be done with a `class_simple` interface like this:

```c
static struct class_simple *foo_class;
...
foo_class = class_simple_create(THIS_MODULE, "foo");
if (IS_ERR(foo_class)) {
     printk(KERN_ERR "Error creating foo class.\n");
     goto error;
}

//  creates a directory in sysfs in /sys/class/foo
```

- When a new device is found by your driver, you assign the minor number and the driver should call the `class_simple_device_add` function like this:

```c
class_simple_device_add(foo_class, MKDEV(FOO_MAJOR, minor), NULL, "foo%d", minor);
```

- This will create a subdirectory under /sys/class/foo called fooN with N being the minor number of the device
- The one file created in this directory called dev is exactly what udev needs to create a device node for your device
- When the driver is unbound from the device, give up the minor number with:

```c
class_simple_device_remove(MKDEV(FOO_MAJOR, minor));
```

And when the driver is being shutdown make sure to destroy the class with:

```c
class_simple_destroy(foo_class);
```

### Dealing with Firmware 

The proper way to load firmware onto a device is to obtain the firmware from user space when you need it. There is a special firmware interface created just for this:

```c
#include <linux/firmware.h>
int request_firmware(const struct firmware **fw, char *name,
                     struct device *device);
```

- The call to request_firmware requests that user space locate and provide a firmware image to the kernel
- `name` should identify the firmware that is desired
- Returns 0 on successful loading
- The `fw` argument is passed to one of these structures:

```c
struct firmware {
     size_t size;
     u8 *data;
};
```

This structure contains the actual data. After sending firmware to the device, release it with:

```c
void release_firmware(struct firmware *fw);
```

This waits on the user space to help, so it is guaranteed to sleep. If your driver cannot sleep when it must ask for firmware, there is an asynchronous alternative (look it up). The firmware subsystem works with sysfs and the hotplug mechanism. When a call is made to request_firmware, a new directory is created in /sys/class/firmware using the devices name. The directory has three attributes: loading, data, and device. Once the sysfs entries have been created, the kernel generates a hotplug event for your device. The environment passed to the hotplug handler includes a variable FIRMWARE,which is set to the name provided to request_firmware. The handler must then locate the firmware file and copy it into the kernel. 

This was another dense chapter haha.

