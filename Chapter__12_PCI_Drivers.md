# LDD3 Chapter 12 Notes

## Chapter 12: PCI Drivers

Chapter Goal: Understand the kernel functions to access the Peripheral Component Interconnect (PCI) bus. PCI used to be very common, now replaced with the PCI-e standard. AMD CPUs now support PCI-e 4.0, and Intel will likely follow suit in a generation or two. Feels weird to say AMD is leading the CPU race. 

First, a little more info on buses. A bus consists of both an electical interface (like the physical slot and specific pins) and a programming interface (all the magic stuff you don't see). This chapter focuses on the programming interface side of the bus. The PCI bus is very well supported by the kernel and replaced the ISA bus.

The ISA bus is very old and no longer used. Here is the wiki article on it:
[Industry Standard Architecture - Wikipedia](https://en.wikipedia.org/wiki/Industry_Standard_Architecture)

### The PCI Interface

PCI describes not only the hardware specifics of how to design for the bus, but also specifies how software should interact as well. We will be looking into more detail on how a PCI driver can find its hardware and gain access to it. To find a PCI device, you could use probing or autodetecting of the IRQ number, but there are better ways that we will cover. 

PCI was designed to replace ISA with three major improvements:

1. Get better performance when transferring data between the computer and its peripherals
   - It uses a higher clock rate (25 or 33 MHz, but can even go higher to 133 Mhz+)
   - It is equipped with a 32-bit bus with a 64-bit extension included in the specs
2. Be as platform independent as possible
   - Supports many more architectures than ISA
3. Simplify adding and removing peripherals to the system
   - Support for autodetecting of interface boards
   - PCI devices are jumperless and automatically configured at boot time
   - The driver accesses configuration info in the device to complete initialization
     - This happens without ANY probing. Nice!

### PCI Addressing

Each PCI peripheral is identified by a:

- bus number
- device number
- function number

The PCI spec allows for a single system to host up to 256 buses, but because this number is often not enough for big systems, you can now have PCI domains with each domain supporting 256 buses. Each bus can host 32 devices. Each device can be a multifunction board with a maximum of 8 functions. Each function can be identified at a hardware level by a 16-bit address - this address is also called a key. The good news is that drivers written for Linux do not need to deal with the binary addresses. Drivers instead use a a specific data structure call `pci_dev` to act on the devices. 

Most systems have several PCI buses. We use a special peripheral device called a bridge to join two buses on a system. The overall layout of a PCI system is then a tree where each bus is connected to an upper-layer bus all the way up to bus 0 at the root of the tree. The bridge structure is very interesting - here is an image with more detail on what the structure looks like (&copy; David A Rusling):

![pci-system.gif](https://tldp.org/LDP/tlk/dd/pci-system.gif)

The 16-bit hardware addresses associated with PCI peripherals are usually hidden within the `pci_dev` object, but are still visible when lists of devices are being used. One example is the output of `lspci` and the layout information in /proc/pci and /proc/bus/pci. The sysfs representation of PCI devices also shows the addressing scheme in addition to the PCI domain info. When the hardware address is displayed, it can be shown as two 8-bit values - the first a bus number and the second a device and function number. It can also be shown as three values - bus, devices, and function. Finally, it can be shown as four values - domain, bus, device, and function. These values are typcially displayed in hex format. Here are some example outputs:

This method splits the numbers into three separate fields:
```bash
$ lspci 

00:00.0 Host bridge: Intel Corporation Device 9b61 (rev 0c)
00:02.0 VGA compatible controller: Intel Corporation Device 9b41 (rev 02)
00:08.0 System peripheral: Intel Corporation Xeon E3-1200 v5/v6 / E3-1500 v5 / 6th/7th Gen Core Processor Gaussian Mixture Model
00:12.0 Signal processing controller: Intel Corporation Device 02f9
00:14.0 USB controller: Intel Corporation Device 02ed
00:14.2 RAM memory: Intel Corporation Device 02ef
00:14.3 Network controller: Intel Corporation Device 02f0
00:17.0 SATA controller: Intel Corporation Device 02d3
00:19.0 Serial bus controller [0c80]: Intel Corporation Device 02c5
00:19.2 Communication controller: Intel Corporation Device 02c7
00:1c.0 PCI bridge: Intel Corporation Device 02bc (rev f0)
00:1d.0 PCI bridge: Intel Corporation Device 02b0 (rev f0)
00:1d.4 PCI bridge: Intel Corporation Device 02b4 (rev f0)
00:1f.0 ISA bridge: Intel Corporation Device 0284
00:1f.3 Audio device: Intel Corporation Device 02c8
00:1f.4 SMBus: Intel Corporation Device 02a3
00:1f.5 Serial bus controller [0c80]: Intel Corporation Device 02a4
01:00.0 PCI bridge: Intel Corporation JHL7540 Thunderbolt 3 Bridge [Titan Ridge 2C 2018] (rev 06)
02:00.0 PCI bridge: Intel Corporation JHL7540 Thunderbolt 3 Bridge [Titan Ridge 2C 2018] (rev 06)
02:01.0 PCI bridge: Intel Corporation JHL7540 Thunderbolt 3 Bridge [Titan Ridge 2C 2018] (rev 06)
02:02.0 PCI bridge: Intel Corporation JHL7540 Thunderbolt 3 Bridge [Titan Ridge 2C 2018] (rev 06)
03:00.0 System peripheral: Intel Corporation JHL7540 Thunderbolt 3 NHI [Titan Ridge 2C 2018] (rev 06)
25:00.0 USB controller: Intel Corporation JHL7540 Thunderbolt 3 USB Controller [Titan Ridge 2C 2018] (rev 06)
26:00.0 Unassigned class [ff00]: Realtek Semiconductor Co., Ltd. RTL8411B PCI Express Card Reader (rev 01)
26:00.1 Ethernet controller: Realtek Semiconductor Co., Ltd. RTL8111/8168/8411 PCI Express Gigabit Ethernet Controller (rev 12)
27:00.0 Non-Volatile memory controller: Samsung Electronics Co Ltd NVMe SSD Controller SM981/PM981
```

This method uses a single 16-bit field to ease parsing and sorting:
```bash
$ cat /proc/bus/pci/devices | cut -f1

0000
0010
0040
0090
00a0
00a2
00a3
00b8
00c8
00ca
00e8
00ec
00f8
00fb
00fc
00fd
2600
2601
2700
00e0
0100
0200
0208
0210
0300
2500
```

```bash
$ tree /sys/bus/pci/devices/

/sys/bus/pci/devices/
├── 0000:00:00.0 -> ../../../devices/pci0000:00/0000:00:00.0
├── 0000:00:02.0 -> ../../../devices/pci0000:00/0000:00:02.0
├── 0000:00:08.0 -> ../../../devices/pci0000:00/0000:00:08.0
├── 0000:00:12.0 -> ../../../devices/pci0000:00/0000:00:12.0
├── 0000:00:14.0 -> ../../../devices/pci0000:00/0000:00:14.0
├── 0000:00:14.2 -> ../../../devices/pci0000:00/0000:00:14.2
├── 0000:00:14.3 -> ../../../devices/pci0000:00/0000:00:14.3
├── 0000:00:17.0 -> ../../../devices/pci0000:00/0000:00:17.0
├── 0000:00:19.0 -> ../../../devices/pci0000:00/0000:00:19.0
├── 0000:00:19.2 -> ../../../devices/pci0000:00/0000:00:19.2
├── 0000:00:1c.0 -> ../../../devices/pci0000:00/0000:00:1c.0
├── 0000:00:1d.0 -> ../../../devices/pci0000:00/0000:00:1d.0
├── 0000:00:1d.4 -> ../../../devices/pci0000:00/0000:00:1d.4
├── 0000:00:1f.0 -> ../../../devices/pci0000:00/0000:00:1f.0
├── 0000:00:1f.3 -> ../../../devices/pci0000:00/0000:00:1f.3
├── 0000:00:1f.4 -> ../../../devices/pci0000:00/0000:00:1f.4
├── 0000:00:1f.5 -> ../../../devices/pci0000:00/0000:00:1f.5
├── 0000:01:00.0 -> ../../../devices/pci0000:00/0000:00:1c.0/0000:01:00.0
├── 0000:02:00.0 -> ../../../devices/pci0000:00/0000:00:1c.0/0000:01:00.0/0000:02:00.0
├── 0000:02:01.0 -> ../../../devices/pci0000:00/0000:00:1c.0/0000:01:00.0/0000:02:01.0
├── 0000:02:02.0 -> ../../../devices/pci0000:00/0000:00:1c.0/0000:01:00.0/0000:02:02.0
├── 0000:03:00.0 -> ../../../devices/pci0000:00/0000:00:1c.0/0000:01:00.0/0000:02:00.0/0000:03:00.0
├── 0000:25:00.0 -> ../../../devices/pci0000:00/0000:00:1c.0/0000:01:00.0/0000:02:02.0/0000:25:00.0
├── 0000:26:00.0 -> ../../../devices/pci0000:00/0000:00:1d.0/0000:26:00.0
├── 0000:26:00.1 -> ../../../devices/pci0000:00/0000:00:1d.0/0000:26:00.1
└── 0000:27:00.0 -> ../../../devices/pci0000:00/0000:00:1d.4/0000:27:00.0

26 directories, 0 files
```

Let's look at one example from these lists. The USB controller is listed in lspci as `0000:00:14.0` which is the same as `00a0` in the /proc/ file when split into domain (16 bits), bus (8 bits), device (5 bits), and function (3 bits). 

The hardware of each PCI board tells us about three address spaces:

1. memory locations
2. I/O ports
3. configuration registers

The first two of these address spaces are shared by all devices on the devices on the same PCI bus. The configuration space is different in that it exploits geographical addressing. Configuration queries address only only slot at a time to prevent collisions. 

When a driver is working with a PCI device, memory and I/O regions are accessed in the usual ways with inb, readb, etc. Configuration transactions are again different from the rest. They are performed by calling specific kernel functions to access configuration registers. Each PCI device can have 4 interrupt pins to the CPU, but these lines must be shareable according to the PCI specification. 

The I/O space in a PCI bus uses a 32-bit address bus and the memory space can be accessed with either 32-bit or 64-bit addresses. 64-bit addresses are available on pretty much all computers these days. That is the standard. 

Addresses are supposed to be unique to one device on the system, but sometimes these addresses can have common (bad!) mappings with other devices when a driver is doing things that it shouldn't be doing. Memory and I/O regions for a specific device can be remapped by using configuration transactions. By doing this, the firmware initializes PCI hardware at system boot, mapping each region to a different address to avoid collisions (there is also ways to do this with hot-pluggable devices). These addresses can be read by the driver in configuration space to eliminate the need for probing. After reading these registers, the driver can safely access the PCI hardware. 

The PCI configuration space consists of 256 bytes for each device function. The layout of configuration registers in standardized as follows:

- Four bytes of the configuration space hold a unique function ID
  - The driver can identify the device by looking for the specific ID for that peripheral
- Each device board is geographically addressed to retrieve its configuration registers
  - Info in these registers can perform normal I/O access without further geographic addressing

# %% Go over this geographic addressing in more detail ^^

Side note: PCI Express devices have 4 KB of configuration space for each function. 

### Boot Time

Let's start with what happens to a PCI device from the time of system boot:

- When power is first applied to a PCI device, the hardware remains inactive
  - The device only responds to configuration transactions
  - There are no memory or I/O ports mapped at this time
  - Interrupt reporting is disabled too

The motherboard will have PCI-aware firmware (typically BIOS) that offers access to the device configuration address space by reading and writing registers in the PCI controller.

- At system boot, the firmware performs configuration transactions with every PCI peripheral in order to allocate a safe place for each address region it offers
  - By the time a driver needs to access a device, memory and I/O regions have already been mapped into the processor's address space

Individual PCI device directories in the sysfs tree can be found in `/sys/bus/pci/devices`. Here is a sample PCI directory from my machine:

```bash
$ tree /sys/bus/pci/devices/0000\:00\:12.0

/sys/bus/pci/devices/0000:00:12.0
├── ari_enabled
├── broken_parity_status
├── class
├── config
├── consistent_dma_mask_bits
├── d3cold_allowed
├── device
├── dma_mask_bits
├── driver_override
├── enable
├── irq
├── local_cpulist
├── local_cpus
├── modalias
├── msi_bus
├── numa_node
├── power
│   ├── async
│   ├── autosuspend_delay_ms
│   ├── control
│   ├── runtime_active_kids
│   ├── runtime_active_time
│   ├── runtime_enabled
│   ├── runtime_status
│   ├── runtime_suspended_time
│   └── runtime_usage
├── remove
├── rescan
├── resource
├── resource0
├── revision
├── subsystem -> ../../../bus/pci
├── subsystem_device
├── subsystem_vendor
├── uevent
└── vendor

2 directories, 34 files
```

The file called `config` is a binary file that enables the raw PCI config info to be read from the device. The files called `vendor`, `deivce`, `subsystem_device`, `subsystem_vendor`, and `class` all refer to specific values of this PCI device. `irq` shows the current IRQ assigned to this PCI device, and `resource` shows the current memory resources allocated by this device. 

### Configuration Registers and Initialization

All PCI devices have at least a 256-byte address space. The first 64 bytes are standardized while the rest are device dependent. Figure 12-2 shows the layout of the device-independent configuration space. The book has a really nice picture of this space, but here is the best online equialent I could find (&copy; Micronet Co.):

![14.png](http://www.mnc.co.jp/english/INtime/faq07-2_kanren/images/14.png)

There are required fields that assert the board's capabilities and whether the other optional fields are even usable. PCI registers are always little-endian ordering. Keep this in mind if a machine that you are using is big-endian. 

We are interested in how a driver can look for its device and how it can access the device's configuration space. Three or five PCI registers identify a device: `vendorID`, `deviceID`, and `class` are the three that must always be used. Everybody assigns read-only values to these registers that a driver can use to look for a device. There are addiational fields called `subsystem vendorID` and `subsystem deviceID` that can be used by a vendor to further differentiate similar devices that might share the first three registers. 

Some more detail on the registers:

```
vendorID
    A 16-bit register that identifies a hardware manufacturer. Example: every Intel
    device is marked with the same vendor number of 0x8086. There is a global registry of 
    such numbers, maintained by the PCI Special Interest Group, and manufacturers must 
    apply to have a unique number assigned to them. Btw - an annual membership to PCI-SIG 
    costs $4k. Not in my budget anytime soon.
    
deviceID
    This is another 16-bit register, selected by the manufacturer. No official 
    registration is required for the device ID. This ID is usually paired with the vendor 
    ID to make a unique 32-bit identifier for a hardware device. The word signature is 
    used to refer to the vendor and device ID pair. A device driver usually relies on the 
    signature to identify its device. 
    
class
    Every peripheral device belongs to a class. The class register is a 16-bit value. The
    top 8 bits identify the base class (aka the group). Example: ethernet and token ring
    are two classes belonging to the network group. The serial and parallel classes 
    belong to the communcation groups. Some drivers can support several similar devices 
    with each having a different signature but all belonging to the same class. These 
    drivers rely on the `class` register to identify their peripherals.
    
subsystem vendorID
subsystem deviceID
    These fields can be used for further identification of a device. If the chip is a
    generic interface chip to a local (onboard) bus, it is often used in several 
    completely different roles, and the driver must identify the actual device it is 
    talking with. The subsystem identifiers are used to this end.
```

Using these 3 or 5 identifiers, a PCI driver can tell the kernel what kind of devices it supports. The struct `pci_device_id` is used to define a list of the different types of PCI devices that a driver supports. The `pci_device_id` struct contains the following fields:

```
__u32 vendor;
__u32 device;
    These specify the PCI vendor and device IDs of a device. If a driver can handle
    any vendor or device ID, the value PCI_ANY_ID should be used for these fields.

__u32 subvendor;
__u32 subdevice;
    These specify the PCI subsystem vendor and subsystem device IDs of a device. If
    a driver can handle any type of subsystem ID, the value PCI_ANY_ID should be
    used for these fields.
    
__u32 class;
__u32 class_mask;
    These two values allow the driver to specify that it supports a type of PCI class
    device. The different classes of PCI devices (a VGA controller is one example)
    are described in the PCI specification. If a driver can handle any type of subsystem
    ID, the value PCI_ANY_ID should be used for these fields.
    
kernel_ulong_t driver_data;
    This value is not used to match a device but is used to hold information that the
    PCI driver can use to differentiate between different devices if it wants to.    
```

There are two helper macros to initialize a `struct pci_device_id` structure. 

```
PCI_DEVICE(vendor, device)
    This creates a struct pci_device_id that matches only the specific vendor and
    device ID. The macro sets the subvendor and subdevice fields of the structure to
    PCI_ANY_ID.
    
PCI_DEVICE_CLASS(device_class, device_class_mask)
    This creates a struct pci_device_id that matches a specific PCI class.
```

An example on using these macros can be found in the following kernel files:

```c
drivers/usb/host/ehci-hcd.c:

static const struct pci_device_id pci_ids[ ] = { {
       /* handle any USB 2.0 EHCI controller */
       PCI_DEVICE_CLASS(((PCI_CLASS_SERIAL_USB << 8) | 0x20), ~0),
       .driver_data = (unsigned long) &ehci_driver,
       },
       { /* end: all zeroes */ }
};

drivers/i2c/busses/i2c-i810.c:

static struct pci_device_id i810_ids[ ] = {
       { PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82810_IG1) },
       { PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82810_IG3) },
       { PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82810E_IG) },
       { PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82815_CGC) },
       { PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82845G_IG) },
       { 0, },
};
```

The two drivers shown above create a list of `struct pci_device_id` structures, with an emtpy structure set to all zeros as the last value in the list. This array od IDs is used in the `struct pci_driver` and to tell user space which devices this specific driver supports. 

### MODULE_DEVICE_TABLE

The `pci_device_id` structure needs to be exported to user space to allow the hotplug and module loading systems know what module works with what hardware devices. There is a macro that will do this - it is called `MODULE_DEVICE_TABLE`. One example of this macro is:

```c
MODULE_DEVICE_TABLE(pci, i810_ids);
```

The statement above creates a new local variable called `__mod_pci_device_table` that points to the list of `struct pci_device_id`. In the kernel build process, the depmod program searches all modules for the symbol `__mod_pci_device_table`. If that symbol is found, it pulls the data out of the moduleand adds it to the file /lib/modules/KERNEL_VERSION/modules.pcimap. After depmod completes, all PCI devices that are supported by modules in the kernel are listed together with their module names in that file. When the kernel tells the hotplug system that a new PCI device has been found, the hotplug system uses the modules.pcimap file to find the proper driver to load.

# %% What if two modules could be a driver for a device? How does the kernel decide which driver to use?

### Registering a PCI Driver

To be registered with the kernel, all PCI drivers must create the `struct pci_driver` structure. This structure has function callbacks and variables that describe the PCI driver to the PCI core. You should know about the following fields in the struct:

```
const char *name;
    This is the name of the driver. It must be unique among all PCI drivers in the kernel
    and is normally set to the same name as the module name of the driver. It shows
    up in sysfs under /sys/bus/pci/drivers/ when the driver is in the kernel.
    
const struct pci_device_id *id_table;
    Pointer to the struct pci_device_id table described earlier in this chapter.

int (*probe) (struct pci_dev *dev, const struct pci_device_id *id);
    This is a pointer to the probe function in the PCI driver. This function is called by 
    the PCI core when it has a struct pci_dev that it thinks this driver wants to 
    control. A pointer to the struct pci_device_id that the PCI core used to make this 
    decision is also passed to this function. If the PCI driver claims the struct pci_dev 
    that is passed to it, it should initialize the device properly and return 0. If the 
    driver does not want to claim the device, or an error occurs, it should return a 
    negative error value. 
    
void (*remove) (struct pci_dev *dev);
    This is a pointer to the function that the PCI core calls when the struct pci_dev is 
    being removed from the system, or when the PCI driver is being unloaded from the
    kernel. 
    
int (*suspend) (struct pci_dev *dev, u32 state);
    This is a pointer to the function that the PCI core calls when the struct pci_dev is 
    being suspended. The suspend state is passed in the state variable. This function is
    optional.
    
int (*resume) (struct pci_dev *dev);
    This is a pointer to the function that the PCI core calls when the struct pci_dev is 
    being resumed. It is always called after suspend has been called. This function is
    optional.
```

Overall, creating a proper `struct pci_driver` only requires four fields to be initialized. An example is:

```c
static struct pci_driver pci_driver = {
     .name = "pci_skel",
     .id_table = ids,
     .probe = probe,
     .remove = remove,
};
```

After creating the `struct pci_driver` structure, register it with the PCI core with a call to `pci_register_driver` using a pointer as follows:

```c
static int __init pci_skel_init(void)
{
     return pci_register_driver(&pci_driver);
}
```

- This function will return a negative error number or 0 if everything registered correctly

At the end of use, the PCI driver needs to be unloaded and the `struct pci_driver` needs to be unregistered from the kernel with a call to `pci_unregister_driver`:

```c
static void __exit pci_skel_exit(void)
{
    pci_unregister_driver(&pci_driver);
}
```

### Old-Style PCI Probing

Ancient kernels before 2.6 the function `pci_register_driver` was not always used in PCI drivers. They would instead check the list of PCI devices or call a function to search the list for them. You can no longer walk through the list of PCI devices. This was done to prevent drivers from crashing the kernel by modifying stuff and behaving badly. There are still existing ways of searching for a device from a list, but doing this is discouraged. If you really need to do this, look at the man pages for the following functions:

```c
struct pci_dev *pci_get_device(unsigned int vendor, unsigned int device,
    struct pci_dev *from);
    
struct pci_dev *pci_get_subsys(unsigned int vendor, unsigned int device,
    unsigned int ss_vendor, unsigned int ss_device, struct pci_dev *from);
    
struct pci_dev *pci_get_slot(struct pci_bus *bus, unsigned int devfn);
```

- Thses functions cannot be called from an interrupt context and should be avoided
- Do not use these methods when creating new drivers

### Enabling the PCI Device

In the `probe` function for the PCI driver, the driver must call the `pci_enable_device` function before accessing any device resource from the memory or I/O regions.

The prototype for the enabling function is:

```c
int pci_enable_device(struct pci_dev *dev);
```

- This function is the one that actually enables the device for use. It performs the waking and can also assign interrupt lines and I/O regions

### Accessing the Configuration Space

After a driver has successfully detected the device and enabled it, you typically want to read or write from the three address spaces:

1. Memory
2. Port
3. Configuration

Accessing the configuration space is vital to the driver because this is the only to find out where the device is mapped in memory and in the I/O space. Linux provides a standard interface to access the configuration space. The configuration space can be accessed with 8, 16, or 32 bit data transfers. The relevant functions to do this are found in `<linux/
pci.h>`:

```
int pci_read_config_byte(struct pci_dev *dev, int where, u8 *val);
int pci_read_config_word(struct pci_dev *dev, int where, u16 *val);
int pci_read_config_dword(struct pci_dev *dev, int where, u32 *val);
    Read one, two, or four bytes from the configuration space of the device identified by 
    dev. The "where" argument is the byte offset from the beginning of the configuration 
    space. The value fetched from the configuration space is returned through the val 
    pointer, and the return value of the functions is an error code. The word and dword 
    functions convert the value just read from little-endian to the native byte order of 
    the processor, so you need not deal with byte ordering (nice!).

int pci_write_config_byte(struct pci_dev *dev, int where, u8 val);
int pci_write_config_word(struct pci_dev *dev, int where, u16 val);
int pci_write_config_dword(struct pci_dev *dev, int where, u32 val);
    Write one, two, or four bytes to the configuration space. The device is identified
    by dev as usual, and the value being written is passed as val. The word and dword   
    functions convert the value to little-endian before writing to the peripheral device.
```

The functions listed above are implemented as inline functions. That means they really call a separate set of functions listed below. Use the functions below if the driver does not have direct access to a `struct pci_dev` at any moment in time:

```c
int pci_bus_read_config_byte (struct pci_bus *bus, unsigned int devfn, int
    where, u8 *val);

int pci_bus_read_config_word (struct pci_bus *bus, unsigned int devfn, int
    where, u16 *val);
 
int pci_bus_read_config_dword (struct pci_bus *bus, unsigned int devfn, int
    where, u32 *val);
```

- All of these are just like the `pci_read_` functions but `struct pci_bus *` and `devfn` variables are needed instead of a `struct pci_dev *`.

```c
int pci_bus_write_config_byte (struct pci_bus *bus, unsigned int devfn, int
    where, u8 val);
     
int pci_bus_write_config_word (struct pci_bus *bus, unsigned int devfn, int
    where, u16 val);
    
int pci_bus_write_config_dword (struct pci_bus *bus, unsigned int devfn, int
    where, u32 val);
```

- All of these are just like the `pci_write_` functions, but `struct pci_bus *` and `devfn` are needed instead of `struct pci_dev *`.


The best way to address the configuration variables using the `pci_read_` functions is by using the symbolic names defined in `<linux/pci.h>`. Example: the following function retrieves the revision ID of a device by passing the symbolic name for `where` to `pci_read_config_byte`:

```c
static unsigned char skel_get_revision(struct pci_dev *dev)
{
     u8 revision;
     
     pci_read_config_byte(dev, PCI_REVISION_ID, &revision);
     return revision;
}
```

### Accessing the I/O and Memory Spaces

A PCI device can have up to six I/O address regions. Each region can have either memory or I/O locations. Most devices implement their I/O registers in memory regions, however, unlike normal memory, I/O registers should not be cached by the CPU because each access could have negative side effects. A PCI device that implements I/O registers as a memory region marks the difference by setting a “memory-is-prefetchable” bit in its configuration register. If the memory region is marked as prefetchable, the CPU can cache its contents and do all sorts of optimization with it. Nonprefetchable memory access can’t be optimized because each access can have side effects, just as with I/O ports.

# %% Why is this and what do we mean by nonprefetchable?

Peripherals that map their control registers to a memory address range declare that range as nonprefetchable, whereas things like video memory on PCI boards is prefetchable.

A PCI interface board reports the size and current location of its regions using configuration registers - there are six 32-bit registers whose symbolic names are PCI_BASE_ADDRESS_0 through PCI_BASE_ADDRESS_5. Since the I/O space in the standard for PCI is a 32-bit address space, it seems intuitive to use the same configuration interface for memory and I/O. If the device uses a 64-bit address bus, it can declare regions in the 64-bit memory space by using two consecutive PCI_BASE_ADDRESS registers for each region with low bits first. It is possible for one device to offer both 32-bit regions and 64-bit regions.

I/O regions for PCI devices are integrated into the generic resource management of the kernel. Thus, there is no need to access the config variables to know where your device is mapped in memory or I/O space. Getting region info can be done with the following functions:

```
unsigned long pci_resource_start(struct pci_dev *dev, int bar);
    The function returns the first address (memory address or I/O port number)
    associated with one of the six PCI I/O regions. The region is selected by the integer     "bar" (the base address register), ranging from 0–5, inclusive.
    
unsigned long pci_resource_end(struct pci_dev *dev, int bar);
    The function returns the last address that is part of the I/O region number bar.
    This is the last usable address, not the first address after the region.
    
unsigned long pci_resource_flags(struct pci_dev *dev, int bar);
    This function returns the flags associated with this resource.
```

Resource flags define some features of the individual resource. For PCI resources associated with PCI I/O regions, the info comes from the base address registers but can also come from other locations not associated with PCI devices. All the resource flags are defined in `<linux/ioport.h>`. Some of the most important flags are:

```
IORESOURCE_IO
IORESOURCE_MEM
    If the associated I/O region exists, one and only one of these flags is set.
    
IORESOURCE_PREFETCH
IORESOURCE_READONLY
    These flags tell whether a memory region is prefetchable and/or write protected.
    The latter flag is never set for PCI resources.
```

By using the `pci_resource_` functions, a driver can ignore the underlying PCI registers because the system already used them to structure resource info. 

# %% So then how would you actually access these things with the driver?

### PCI Interrupts

PCI is easy with interrupts! By the time your system has booted, the firmware has already assigned a unique interrupt number to the device and the driver just gets to use it (easy!). The interrupt number is stored in config register 60 (called PCI_INTERRUPT_LINE). This register is one byte wide. This means you could have 256 interrupt lines, but the real limit depends on the CPU being used. The driver should not check the interrupt number, but simply use `PCI_INTERRUPT_LINE` instead, because it will be right (easy!). 

If your device does not support interrupts, register 61 will be set to 0 (this register is also called `PCI_INTERRUPT_PIN`). It will be nonzero if interrupts are supported. PCI specific code for dealing with interrupts jsut needs to read the config byte to obtain the interrupt number that is saved in a local variable. An example is shown below:

```c
result = pci_read_config_byte(dev, PCI_INTERRUPT_LINE, &myirq);
if (result) {
    /* deal with error */
}
```

Additional fun notes on PCI devices:

- A PCI connector has four interrupt pins
  - boards can use any/all of them
  - each is routed individually to the mobo
- The read-only configuration register located at PCI_INTERRUPT_PIN is used to tell the computer which single pin is actually used
  - Different devices on the same device board can use different interrupt pins or share the same one
- The PCI_INTERRUPT_LINE register is read/write on the hardware itself
  - value is assigned by firmware at boot
  - For the device driver, however, the PCI_INTERRUPT_LINE register is read-only
    - Don't mess with this in your driver
    - Sometimes in new machines this can actually be R/W without a reboot

### Hardware Abstractions

The mechanism used to implement hardware abstraction is a typical structure method. It’s a powerful technique that adds a slight overhead of dereferencing a pointer to the normal overhead of a function call. In the case of PCI management, the only hardware-dependent operations are the ones that read and write configuration registers, because everything else in the PCI world is accomplished by directly reading and writing the I/O and memory address spaces which are under direct control of the CPU.

The structure for configuration register access includes 2 fields only and is defined in `<linux/pci.h>`:

```c
struct pci_ops {
     int (*read)(struct pci_bus *bus, unsigned int devfn, int where, int size,
        u32 *val);
     int (*write)(struct pci_bus *bus, unsigned int devfn, int where, int size,
        u32 val);
};
```

The two functions that act on the PCI configuration space have more overhead than dereferencing a pointer. They use cascading pointers due to the high object-orientedness of the code, but the overhead is not an issue in operations that are performed rarely and never in speed-critical paths. The implementation of pci_read_config_byte(dev, where, val), for example, expands to be:

```c
dev->bus->ops->read(bus, devfn, where, 8, val);
```

The various PCI buses in the system are detected at system boot, and that’s when the `struct pci_bus` items are created and associated with their features ()including the `ops` field).


### The Rest of This Chapter

The remainder of this chapter goes into minor detail about a bunch of outdated ports that I have never heard of and would not be practical to learn anymore, thus I have omitted them from these notes. Some modern motherboards still have legacy PCI slots on them, and some scientific equipment still requires PCI slots to interface with. As such, the PCI discussion in this chapter is still relevant to the scientific reader, but more research should be done to learn more about PCI-e to learn about what most people are using today.

The following presentation is one that I found useful for making an actual PCI/PCI-e driver from scratch (credit Eli Billauer for the nice presentation):

[PCI/PCI-e Driver Getting Started](http://www.haifux.org/lectures/256/haifux-pcie.pdf)


























