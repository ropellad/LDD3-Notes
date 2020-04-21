# LDD3 Chapter 17 Notes

## Chapter 17: Network Drivers

Chapter goal: Understand how networking works in Linux at the kernel level

Note: Here is the repo I used for a working snull driver:
[GitHub - martinezjavier/ldd3: Linux Device Drivers 3 examples updated to work in recent kernels](https://github.com/martinezjavier/ldd3)
This compiled and ran just fine on 5.3.0-7648.

A network interface exchanges packets of information with the outside world. Network drivers are different from block devices in that they receive packets asynchronously from the outside world. A block driver is asked to send a buffer toward the kernel. A network device ask to push incoming packets toward the kernel. The network API looks different than the stuff we have seen previously with the kernel. 

The network subsystem of the kernel is designed to be protocol independent. Interaction between a network driver and the kernel properly deals with one network packet at a time. This enables protocol issues to be hidden neatly from the driver and the physical transmission to be hidden from the protocol. 

We will be working with an example of a memory-based modularized network interface called snull. It uses the Ethernet hardware protocol and transmits IP packets. Some important terminology before the main content:

- octet - a group of eight bits in the networking world
  - Don't really use the term bytes
- header - a set of octets prepended to a packet as it is passed through the various layers of the networking subsystem
  - Example: when an application sends a block of data through a TCP socket, the networking subsystem breaks that data up into packets and puts a TCP header at the beginning
  - Network drivers need not concern themselves with higher-level headers
     - They often must be involved in the creation of the hardware-level header

### How snull Is Designed

snull was designed to be hardware independent. It might look like a loopback interface, but it is not. It simulates conversations with real remote hosts to demonstrate the task of writing a network driver. snull only supports IP traffic. This is because of the internal workings of the interface. Real interfaces don't depend on the protocol being transmitted. This will not affect the segments of code in these notes. 

### Assigning IP Numbers

snull toggles the least significant bit of the third octet of both the source and destination addresses. It changes both the network number and the host number of class C IP numbers. Packets sent to network A appear as packets belonging to network B. To avoid a lot of numbers to confuse myself and other readers, the authors assigned symbolic names to the IP numbers involved. Here are the symbolic names:

- `snullnet0` is the network that is connected to the sn0 interface
- `snullnet1` is the network that is connected to the sn1 interface
- `local0` is the IP address assigned to the `sn0` interface
  - It belongs to `snullnet0`
- The address associated with `sn1` is `local1`
- `remote0` is a host in `snullnet0`
- Any packet sent to `remote0` reaches `local1` after its network address has been modified by the interface code
- The host `remote1` belongs to `snullnet1`

These are the network numbers to add into /etc/networks to be able to use snull:

```
snullnet0     192.168.0.0
snullnet1     192.168.1.0
```

And these are the host numbers to put into /etc/hosts:

```
192.168.0.1     local0
192.168.0.2     remote0
192.168.1.2     local1
192.168.1.1     remote1
```

If you use different numbers, ensure that the host portion of local0 is the same as that of remote1, and the host portion of local1 is the same as that of remote0. 

You can then set up the interfaces for operation by using:

```shell
$ ifconfig sn0 local0
$ ifconfig sn1 local1
```

Note: This did not appear to work on my recent version of the Linux kernel. There are other ways of doing this now - it seems like ifconfig has been largely replaced by ip addr

### The Physical Transport of Packets

snull interfaces belong to the Ethernet class. Ethernet is a very common standard now, and it is easy to listen to the two interfaces with tcpdump. Snull also only transmits IP packets and destroys all non-IP packets. Other protocols will require modifying the source code. 

### Connecting to the Kernel

#### Device Registration

Network drivers are initialized in different ways than char or block drivers. There is no equivalent for major and minor numbers for network interfaces. Instead, the driver inserts a data structure for each newly detected interface into a global list of network devices. 

Each interface is described by a `stract net_device` item. The snull driver keeps pointers to two of these structures in a simple array:

```c
struct net_device *snull_devs[2];
```

- This structure contains a kobject so it is reference-counted and exported via sysfs
- Must be allocated dynamically
- Do this allocation with:

```c
struct net_device *alloc_netdev(int sizeof_priv,
                                const char *name,
                                void (*setup)(struct net_device *));
```

- `sizeof_priv` is the size of the driver’s private data area
- `name` is the name of this interface seen by user space
- `setup` is a pointer to an initialization function called to set up the rest of the net_device structure

snull allocated its two device structures in this way:

```c
snull_devs[0] = alloc_netdev(sizeof(struct snull_priv), "sn%d",
    snull_init);
snull_devs[1] = alloc_netdev(sizeof(struct snull_priv), "sn%d",
    snull_init);
if (snull_devs[0] = = NULL || snull_devs[1] = = NULL)
    goto out;
```

To allocate a network device with the eth%d name argument (%d chooses the next available number), the networking subsystem provides a helper function:

```c
struct net_device *alloc_etherdev(int sizeof_priv);
```

- This provides its own initialization function (ether_setup)
- snull does not use this (it uses alloc_netdev instead) because our example works at a lower level to give us control of the name of the interface

Once the net_device structure is initialized, we register it with register_netdev like this:

```c
for (i = 0; i < 2; i++)
     if ((result = register_netdev(snull_devs[i])))
        printk("snull: error %i registering device \"%s\"\n",
               result, snull_devs[i]->name);
```

- Again, don't register until everything is completely initialized!

### Initializing Each Device

Complete initialization of snull uses the snull_init function that mainly looks like this:

```c
ether_setup(dev); /* assign some of the fields */

dev->open = snull_open;
dev->stop = snull_release;
dev->set_config = snull_config;
dev->hard_start_xmit = snull_tx;
dev->do_ioctl = snull_ioctl;
dev->get_stats = snull_stats;
dev->rebuild_header = snull_rebuild_header;
dev->hard_header = snull_header;
dev->tx_timeout = snull_tx_timeout;
dev->watchdog_timeo = timeout;
/* keep the default flags, just add NOARP */
dev->flags |= IFF_NOARP;
dev->features |= NETIF_F_NO_CSUM;
dev->hard_header_cache = NULL; /* Disable caching */
```

- This init function mainly stores pointers to our various driver functions
- We do not use ARP because there is nobody remote to connect to

### Module Unloading

This is fairly typical for a kernel module. It unregisters the interfaces, performs whatever internal cleanup is required, and releases the net_device structure back to the system with:

```c
void snull_cleanup(void)
{
     int i;
     
     for (i = 0; i < 2; i++) {
         if (snull_devs[i]) {
             unregister_netdev(snull_devs[i]);
             snull_teardown_pool(snull_devs[i]);
             free_netdev(snull_devs[i]);
         }
     }
     return;
}
```

- `unregister_netdev` removes the interface from the system
- `free_netdev` returns the net_device structure to the kernel

### net_device Structure in More Detail

#### Global Info

The first part of the `struct net_device` has the following fields:

- `char name[IFNAMSIZ];`
  - The name of the device
  - Can follow a %d format string
- `unsigned long state;`
  - The device state
  - Composed of several flags
  - Use utility functions to manipulate these
- `struct net_device *next;`
  - Pointer to the next device in the global linked list
  - Don't touch this
- `int (*init)(struct net_device *dev);`
  - The initialization function
  - If set, the function is called by register_netdev to complete the initialization of the net_device structure
  - This is no longer used. Do all initialization before registering device

#### Hardware Info

These fields contain low-level hardware info for simple devices (not used much anymore):

- `unsigned long rmem_end;` and
- `unsigned long rmem_start;` and
- `unsigned long mem_end;` and
- `unsigned long mem_start;`
  - Device memory info
  - These hold the beginning and ending addresses of the shared memory used by the device
- `unsigned long base_addr;`
  - The I/O base address of the network interface
  - Assigned by the driver during the device probe
  - ifconfig command can be used to display or modify the current value
- `unsigned char irq;`
  - The assigned interrupt number
  - dev->irq is printed by ifconfig when interfaces are listed
  - Set at boot or load time and modified later using ifconfig
- `unsigned char if_port;`
  - The port in use on multiport devices
  - Used on ports that support both coaxial and twisted pair ethernet connections
    - I think all of them are pretty much twisted pair these days
- `unsigned char dma;`
  - The DMA channel allocated by the device
  - Only really used with peripheral buses

#### Interface Info

Ethernet cards can rely on the ether_setup function for most of the interface info fields, but the flags and dev_addr fields are device specific and must be explicitly assigned at initialization time. Non-ethernet interfaces need some helper functions like:

- `void ltalk_setup(struct net_device *dev);`
  - Sets up the fields for a LocalTalk device (wtf is that? [LocalTalk - Wikipedia](https://en.wikipedia.org/wiki/LocalTalk))
- `void fc_setup(struct net_device *dev);`
  - Initializes fields for fiber-channel devices
- `void fddi_setup(struct net_device *dev);`
  - Configures an interface for a Fiber Distributed Data Interface (FDDI) network
- `void hippi_setup(struct net_device *dev);`
  - Prepares fields for a High-Performance Parallel Interface (HIPPI) high-speed interconnect driver
- `void tr_setup(struct net_device *dev);`
  - Handles setup for token ring network interfaces

I am sure most of these are outdated now and there are different ways of doing this. If you have a different interface than any of these, there are MANY fields you need to assign by hand, with additional flags as well. I am not going to write them all down here, but just know that they exist in a quite-extensive list in the book for this chapter. Honestly, try to avoid inventing a new interface and stick with a well-documented one. It will save many headaches. 

### The Device Methods

Device methods for a network interface can be divided into two groups: 

1. fundamental - methods that are needed to be able to use the interface
2. optional - methods that implement more advanced functionalities that are not strictly required

Here are the fundamental methods:

- `int (*open)(struct net_device *dev);`
  - Opens the interface
  - ifconfig activates it
  - This should register any system resources needed, turn on the hardware, and perform any other setup required for operation
- `int (*stop)(struct net_device *dev);`
  - Stops the interface
  - Should reverse operations performed at open time
- `int (*hard_start_xmit) (struct sk_buff *skb, struct net_device *dev);`
  - Initiates the transmission of a packet
- `int (*hard_header) (struct sk_buff *skb, struct net_device *dev, unsigned short type, void *daddr, void *saddr, unsigned len);`
  - Builds the hardware header from the source and destination hardware addresses that were previously retrieved
  - Organizes the information passed to it as arguments into an appropriate, device-specific hardware header
  - eth_header is the default function for Ethernet-like interfaces
    - ether_setup assigns this field accordingly
- `int (*rebuild_header)(struct sk_buff *skb);`
  - Rebuilds the hardware header after ARP resolution completes but before a packet is transmitted
- `void (*tx_timeout)(struct net_device *dev);`
  - Called by the networking code when a packet transmission fails to complete within a reasonable period
  - It needs to handle the problem and resume packet transmission if possible
- `struct net_device_stats *(*get_stats)(struct net_device *dev);`
  - Called when an application needs to get statistics for the interface
- `int (*set_config)(struct net_device *dev, struct ifmap *map);`
  - Changes the interface configuration
  - The entry point for configuring the driver
  - The I/O address for the device and its interrupt number can be changed at runtime using set_config
  - Most drivers for modern hardware DO NOT USE THIS

There are also many more optional methods that can interact with things like the MAC address, header, ioctl, and other things. I will not list them all here, but the book has a rather large list for this stuff as well.

#### Utility Fields

The remaining fields used here hold useful status info for programs like ifconfig and netstat to provide the user with info. Important fields to assign values to:

- `unsigned long trans_start;` and 
- `unsigned long last_rx;`
  - Fields that hold a jiffies value
  - The driver is responsible for updating these values when transmission begins and when a packet is received
  - trans_start value is used by the networking subsystem to detect transmitter lockups
  - last_rx is currently unused (looks like kernel 4.13 got rid of this field)
- `int watchdog_timeo;`
  - Minimum time (in jiffies) that should pass before the networking layer decides that a transmission timeout has occurred and calls the driver’s tx_timeout function
- `void *priv;`
  - The equivalent of filp->private_data
  - Use netdev_priv to access this data
- `struct dev_mc_list *mc_list;` and `int mc_count;`
  - Fields that handle multicast transmission
  - mc_count is the count of items in mc_list
- `spinlock_t xmit_lock;` and `int xmit_lock_owner;`
  - The xmit_lock is used to avoid multiple simultaneous calls to the driver’s hard_start_xmit function
  - Don't change these fields!
- Other fields in the `struct net_device` are not used by network drivers, so they are not listed here

### Opening and Closing

snull can probe for the interface at module load time or at kernel boot. Before carrying packets, the kernel must open it and assign an address to it. The kernel opens or closes an interface with ifconfig (which doesn't seem to be working properly on my machine :cry: )

When ifconfig is used to assign an address to the interface, it performs two tasks:

1. It assigns the address by means of ioctl(SIOCSIFADDR) (Socket I/O Control Set Interface Address)
2. It sets the IFF_UP bit in dev->flag by means of ioctl(SIOCSIFFLAGS) (Socket I/O Control Set Interface Flags) to turn the interface on

This first command does nothing with the device, while the second calls the open method for the device. When the interface is shut down, ifconfig uses ioctl(SIOCSIFFLAGS) to clear IFF_UP and the stop method is called for the device.

Both methods return 0 on success and negative error. Network drivers need to perform a few extra steps at open time relative to other drivers. Notably:

1. The hardware (MAC) address needs to be copied from the hardware device to dev->dev_addr before the interface can communicate with the outside world
   - The hardware address can then be copied to the device at open time
3. Start the interface’s transmit queue (allowing it to accept packets for transmission) once it is ready to start sending data

The kernel provides a function to start the queue:

```c
void netif_start_queue(struct net_device *dev);
```

And overall, the open function in snull looks like:

```c
int snull_open(struct net_device *dev)
{
     /* request_region( ), request_irq( ), .... (like fops->open) */
     
     /*
     * Assign the hardware address of the board: use "\0SNULx", where
     * x is 0 or 1. The first byte is '\0' to avoid being a multicast
     * address (the first byte of multicast addrs is odd).
     */
     memcpy(dev->dev_addr, "\0SNUL0", ETH_ALEN);
     if (dev = = snull_devs[1])
          dev->dev_addr[ETH_ALEN-1]++; /* \0SNUL1 */
     netif_start_queue(dev);
     return 0;
}
```

And the close function is thus:

```c
int snull_release(struct net_device *dev)
{
     /* release ports, irq and such -- like fops->close */
     netif_stop_queue(dev); /* can't transmit any more */
     return 0;
}
```

- netif_start_queue must be called when the interface is closed, but it can also be used to temporarily stop transmission (will cover this next)

### Packet Transmission

The most important tasks performed by network interfaces are data transmission
and reception. We start with transmission:

Transmission refers to the act of sending a packet over a network link. Whenever the kernel needs to transmit a data packet, it calls the driver’s hard_start_transmit method to put the data on an outgoing queue. Each packet handled by the kernel is contained in a socket buffer structure called `struct sk_buff`. This is just a packet and has nothing to do with sockets... or socks. A pointer to a sk_buff is usually called skb, and the book uses this in sample code. A few facts on socket buffers:

- The socket buffer passed to hard_start_xmit contains the physical packet as it should appear on the media, complete with the transmission-level headers
- The interface thus doesn’t need to modify the data being transmitted
- skb->data points to the packet being transmitted
- skb->len is its length in octets
- “Scatter/Gather I/O makes everything more complicated
  - We will get to this later

snull packet transmission code looks like this:

```c
int snull_tx(struct sk_buff *skb, struct net_device *dev)
{
     int len;
     char *data, shortpkt[ETH_ZLEN];
     struct snull_priv *priv = netdev_priv(dev);
     
     data = skb->data;
     len = skb->len;
     if (len < ETH_ZLEN) {
         memset(shortpkt, 0, ETH_ZLEN);
         memcpy(shortpkt, skb->data, skb->len);
         len = ETH_ZLEN;
         data = shortpkt;
     }
     dev->trans_start = jiffies; /* save the timestamp */
     
     /* Remember the skb, so we can free it at interrupt time */
     priv->skb = skb;
     
     /* actual deliver of data is device-specific, and not shown here */
     snull_hw_tx(data, len, dev);
     
     return 0; /* Our simple device can not fail */
}
```

- This function performs some sanity checks on the packet
- It also transmits the data through the hardware-related function
- The return value from hard_start_xmit should be 0 on success
- The driver must free the skb at the end

### Controlling Transmission Concurrency

The hard_start_xmit function is protected from concurrent calls by a spinlock (xmit_lock) in the net_device structure. As soon as the function returns it can be called again. Note that when it reutrns, the software is done instructing the hardware about packet transmission, but hardware transmission will likely not have been completed. With snull this is not an issue because the CPU does all the work so packet transmission is complete before the transmission function returns.

With real hardware this is different and when the memory that stores outgoing packets is exhausted, the driver needs to tell the networking system not to start any more transmissions until the hardware is ready to accept new data. You do this by calling netif_stop_queue to stop the queue. To later restart the queue, use:

```c
void netif_wake_queue(struct net_device *dev);
```

Modern network hardware maintains an internal queue with multiple packets to transmit. This gives better performance. If you need to disable packet transmission from anywhere other than the hard_start_xmit function, use this function instead:

```c
void netif_tx_disable(struct net_device *dev);
```

This function behaves a lot like netif_stop_queue but adds a feature: it ensures that  your hard_start_xmit method is not running on another CPU when it returns. The queue can be restarted with netif_wake_queue.

### Transmission Timeouts (go sit in the corner, transmisssions)

Sometimes things don't go right with your product. Marketing calls this "added features" but engineers know better. 

Network drivers can set watchdog timers (in jiffies) to distinguish between normal system delays and actual problems. If the current system time exceeds the device's trans_start time by the timeout period, the networking layer eventually calls the driver’s tx_timeout method. This method needs to be the janitor and clean stuff up and get it working again. 

snull can simulate transmitter lockups, which is controlled by two load-time parameters:

```c
static int lockup = 0;
module_param(lockup, int, 0);

static int timeout = SNULL_TIMEOUT;
module_param(timeout, int, 0);
```

If you load the driver with parameter lockup=n, a lockup is simulated every n packets. The snull transmission timeout handler looks like this:

```c
void snull_tx_timeout (struct net_device *dev)
{
    struct snull_priv *priv = netdev_priv(dev);
    PDEBUG("Transmit timeout at %ld, latency %ld\n", jiffies,
        jiffies - dev->trans_start);
      /* Simulate a transmission interrupt to get things moving */
     priv->status = SNULL_TX_INTR;
     snull_interrupt(0, dev, NULL);
     priv->stats.tx_errors++;
     netif_wake_queue(dev);
     return;
} 
```

When a timeout happens in snull, the driver calls snull_interrupt to fill in the missing interrupt and restarts the transmit queue with netif_wake_queue.

### Scatter/Gather I/O

Creating a packet for transmission involves assembling multiple pieces. If you are taking data and headers from all sorts of places there could be a lot of copying going on, but if the interface can perfrom scatter/gather I/O, the packet does not need to be assembled into a single chunk and a lot of the copying can be avoided. You can have zero-copy transmission of network data directly from user-space buffers. Wow!

The NETIF_F_SG bit in the features field of your device structure needs to be set to do this. You then need to look at a special shared info field within the skb to determine whether the packet is made up of a single fragment or many and to find the scattered fragments if needed. There is a macro to access this info (yay!). It is called skb_shinfo. Here is the usage:

```c
if (skb_shinfo(skb)->nr_frags = = 0) {
     /* Just use skb->data and skb->len as usual */
}
```

- `nr_frags` tells how many fragments have been used to build the packet
  - 0 indicates a single packet
  - If nonzero,  your driver must pass through and arrange to transfer each individual fragment
  - The data field of the skb structure points conveniently to the first fragment 
  - The length of the fragment must be calculated by subtracting skb->data_len from skb->len
  - The remaining fragments are to be found in an array called frags 
  - Each entry in frags is an skb_frag_struct structure that looks like this:

```c
struct skb_frag_struct {
    struct page *page;
    __u16 page_offset;
    __u16 size;
};
```

- Loop through the fragments, mapping each for a DMA transfer and not forgetting the first fragment
- Hardware must then must assemble the fragments and transmit them as a single packet
- Some could be in high memory if NETIF_F_HIGHDMA feature flag set beware!

### Packet Reception (the packet wedding was ok, but the reception was great!)

Receiving data from the network is trickier than transmitting it. sk_buff must be allocated and handed off to the upper layers from within an atomic context and this makes it more difficult than transmitting. There are two modes of packet reception that may be implemented by network drivers:

1. interrupt driven (most use this)
2. polled (some implement this in addition to 1)

snull uses the first method. snull_rx is called from the snull interrupt handler after the hardware has received the packet. snull_rx receives a pointer to the data and the length of the packet. Its only job is to send the packet and some additional information to the upper layers of networking code. Here is the snull_rx code:

```c
void snull_rx(struct net_device *dev, struct snull_packet *pkt)
{
     struct sk_buff *skb;
     struct snull_priv *priv = netdev_priv(dev);
     
     /*
     * The packet has been retrieved from the transmission
     * medium. Build an skb around it, so upper layers can handle it
     */
     skb = dev_alloc_skb(pkt->datalen + 2);
     if (!skb) {
         if (printk_ratelimit( ))
            printk(KERN_NOTICE "snull rx: low on mem - packet dropped\n");
         priv->stats.rx_dropped++;
         goto out;
     }
     memcpy(skb_put(skb, pkt->datalen), pkt->data, pkt->datalen);
     
     /* Write metadata, and then pass to the receive level */
     skb->dev = dev;
     skb->protocol = eth_type_trans(skb, dev);
     skb->ip_summed = CHECKSUM_UNNECESSARY; /* don't check it */
     priv->stats.rx_packets++;
     priv->stats.rx_bytes += pkt->datalen;
     netif_rx(skb);
  out:
     return;
}
```

To use this as a template and rework it for your needs:

- Allocate a buffer to hold the packet
  - dev_alloc_skb needs to know the data length
- The return value from dev_alloc_skb must be checked
  - printk_ratelimit helps prevent spamming error messages
- Packet data is copied into the buffer by calling memcpy
  - skb_put updates the end-of-data pointer in the buffer and returns a pointer to the newly created space
- The network layer needs to have some information spelled out before it can make sense of the packet
  - dev and protocol fields must be assigned before the buffer is passed 
  - eth_type_trans finds an appropriate value to put into protocol
- We then need to specify how checksumming is to be performed on the packet
  - Possible policies are:
    - CHECKSUM_HW
      - The device has already performed checksums in hardware
    - CHECKSUM_NONE
      - Checksums have not yet been verified, and the task must be accomplished by system software
    - CHECKSUM_UNNECESSARY
      - Don’t do any checksums. This is the policy in snull and in the loopback interface
- The driver then updates its statistics counter to record that a packet has been received
  - rx_packets, rx_bytes, tx_packets, and tx_bytes fields must be updated
- Last step is to hand off the socket buffer with netif_rx
  - Returns 0 on success
  - Other return values can indicate congestion levels or packing drop

### The Interrupt Handler

Hardware interrupts can signal one of two events:

1. A new packet has arrived
2. Transmission of an outgoing packet is complete

Network interfaces can also generate interrupts to signal errors.

The usual interrupt routine can decide which of the two events happens by checking a status register on the physical device. snull does this with dev->priv. The interrupt handler looks like this:

```c
static void snull_regular_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
     int statusword;
     struct snull_priv *priv;
     struct snull_packet *pkt = NULL;
     /*
     * As usual, check the "device" pointer to be sure it is
     * really interrupting.
     * Then assign "struct device *dev"
     */
     struct net_device *dev = (struct net_device *)dev_id;
     /* ... and check with hw if it's really ours */
     
     /* paranoid */
     if (!dev)
        return;
        
     /* Lock the device */
     priv = netdev_priv(dev);
     spin_lock(&priv->lock);
     
     /* retrieve statusword: real netdevices use I/O instructions */
     statusword = priv->status;
     priv->status = 0;
     if (statusword & SNULL_RX_INTR) {
         /* send it to snull_rx for handling */
         pkt = priv->rx_queue;
         if (pkt) {
             priv->rx_queue = pkt->next;
             snull_rx(dev, pkt);
         }
     }
     if (statusword & SNULL_TX_INTR) {
         /* a transmission is over: free the skb */
         priv->stats.tx_packets++;
         priv->stats.tx_bytes += priv->tx_packetlen;
         dev_kfree_skb(priv->skb);
     }
     /* Unlock the device and we are done */
     spin_unlock(&priv->lock);
     if (pkt) snull_release_buffer(pkt); /* Do this outside the lock! */
     return;
}
```

Notes on the handler:

- Its first job is to retrieve a pointer to the correct struct net_device
  - Comes from dev_id pointer
- In the transmission done situation:
  - The statistics are updated
  - dev_kfree_skb is called to return the socket buffer to the system
  - Restart the transmission queue with netif_wake_queue if needed
- In the reception situation:
  - No special interrupt handling needed
  - snull_rx is all that is needed

### Receive Interrupt Mitigation

With high-bandwidth interfaces that recieve thousands of packets per second, an interrupt interface might not be good because it will bog down the system with interrupts. 

To fix this, an alternative interface called NAPI was created that uses polling. It looks like NAPI is still around today, and it is a way to use interrupt handling for low-volume traffic and then switch to polling automatically when the load becomes too high. 

The snull driver can operate in NAPI mode by setting the use_napi parameter to nonzero:

```c
if (use_napi) {
     dev->poll = snull_poll;
     dev->weight = 2;
}
```

- `poll` must be set to your driver’s polling function
- `weight` describes the relative importance of the interface
  - 10 MBps Ethernet interfaces set weight to 16
  - faster interfaces use 64

snull also changes its interrupt handler to stop tracking interrupts and tell the kernel that it is time to start polling:

```c
if (statusword & SNULL_RX_INTR) {
     snull_rx_ints(dev, 0); /* Disable further interrupts */
     netif_rx_schedule(dev);
}
```

And the snull implementation of the poll method looks like:

```c
static int snull_poll(struct net_device *dev, int *budget)
{
     int npackets = 0, quota = min(dev->quota, *budget);
     struct sk_buff *skb;
     struct snull_priv *priv = netdev_priv(dev);
     struct snull_packet *pkt;
     
     while (npackets < quota && priv->rx_queue) {
         pkt = snull_dequeue_buf(dev);
         skb = dev_alloc_skb(pkt->datalen + 2);
         if (! skb) {
             if (printk_ratelimit( ))
                printk(KERN_NOTICE "snull: packet dropped\n");
             priv->stats.rx_dropped++;
             snull_release_buffer(pkt);
             continue;
         }
         memcpy(skb_put(skb, pkt->datalen), pkt->data, pkt->datalen);
         skb->dev = dev;
         skb->protocol = eth_type_trans(skb, dev);
         skb->ip_summed = CHECKSUM_UNNECESSARY; /* don't check it */
         netif_receive_skb(skb);
         /* Maintain stats */
         npackets++;
         priv->stats.rx_packets++;
         priv->stats.rx_bytes += pkt->datalen;
         snull_release_buffer(pkt);
     }
     /* If we processed all packets, we're done; tell the kernel and reenable ints */
     *budget -= npackets;
     dev->quota -= npackets;
     if (! priv->rx_queue) {
         netif_rx_complete(dev);
         snull_rx_ints(dev, 1);
         return 0;
     }
     /* We couldn't process everything. */
     return 1;
}
```

- The networking subsystem guarantees that any given device’s poll method will not be called concurrently on more than one processor
- BUT calls to poll can still happen concurrently with calls to your other device methods

### Changes in Link State

Most networking technologies involving an actual, physical connection provide a carrier state. It means that hardware is present and ready to run (like your cable is physically plugged in correctly). Drivers can change that state explicitly with:

```c
void netif_carrier_off(struct net_device *dev);
void netif_carrier_on(struct net_device *dev);
```

If your driver detects a lack of carrier on one of its devices, it should call netif_carrier_off to inform the kernel of this change. When the carrier returns, turn it back on. There is also an integer function to test carrier state with:

```c
int netif_carrier_ok(struct net_device *dev);
```

### The Socket Buffers

We will only go over the most important fields of socket buffers. A full understanding is useful but not required for writing drivers.

- `struct net_device *dev;`
  - The device receiving or sending this buffer
- `union { /* ... */ } h;` and 
- `union { /* ... */ } nh;` and 
- `union { /*... */} mac;`
  - Pointers to the various levels of headers contained within the packet
  - `h` hosts pointers to transport layer headers
  - `nh` includes network layer headers 
  - `mac` collects pointers to link-layer headers
- `unsigned char *head;` and
- `unsigned char *data;` and 
- `unsigned char *tail;` and 
- `unsigned char *end;` 
  - Pointers used to address the data in the packet
  - `head` points to the beginning of the allocated space
  - `data` is the beginning of the valid octets
  - `tail` is the end of the valid octets
  - `end` points to the maximum address `tail` can reach
- `unsigned int len;` and `unsigned int data_len;`
  - `len` is the full length of the data in the packet
  - `data_len` is the length of the portion of the packet stored in separate fragments
    - 0 unless scatter/gather I/O is used
- `unsigned char ip_summed;`
  - The checksum policy for this packet
- `unsigned char pkt_type;`
  - Packet classification used in its delivery
  - Driver is responsible for setting it
  - Possible values:
    - PACKET_HOST this packet is for me
    - PACKET_OTHERHOST this packet is not for me
    - PACKET_BROADCAST
    - PACKET_MULTICAST
  - eth_type_trans modifies this for you
  - DO NOT MODIFY YOURSELF
- `shinfo(struct sk_buff *skb);` and
- `unsigned int shinfo(skb)->nr_frags;` and
- `skb_frag_t shinfo(skb)->frags;`
  - There are several fields in this structure, but most of them are beyond the scope of this book
  - We saw previously `nr_frags` and `frags` for S/G I/O

### Functions Acting on Socket Buffers

These are some of the most interesting functions that operate on socket buffers:

- `struct sk_buff *alloc_skb(unsigned int len, int priority);` and
- `struct sk_buff *dev_alloc_skb(unsigned int len);`
  - Allocate a buffer
  - alloc_skb allocates a buffer and initializes both skb->data and skb->tail to skb->head
  - dev_alloc_skb is a shortcut that calls alloc_skb with GFP_ATOMIC priority and reserves some space between skb->head and skb->data
    - Used for operations in the network layer
    - Driver should not touch this space
- `void kfree_skb(struct sk_buff *skb);` and
- `void dev_kfree_skb(struct sk_buff *skb);` and
- `void dev_kfree_skb_irq(struct sk_buff *skb);` and
- `void dev_kfree_skb_any(struct sk_buff *skb);`
  - Free a buffer
  - dev_kfree_skb for noninterrupt context
  - dev_kfree_skb_irq for interrupt context
  - dev_kfree_skb_any for code that can run in either context
- `unsigned char *skb_put(struct sk_buff *skb, int len);` and
- `unsigned char *__skb_put(struct sk_buff *skb, int len);`
  - Update the tail and len fields of the sk_buff structure
  - They are used to add data to the end of the buffer
  - `skb_put` checks to be sure that the data fits in the buffer
  - `__skb_put` omits the buffer check
- `unsigned char *skb_push(struct sk_buff *skb, int len);` and
- `unsigned char *__skb_push(struct sk_buff *skb, int len);`
  - Functions to decrement skb->data and increment skb->len
  - The return value points to the data space just created
  - `__skb_push` differs in that it does not check for adequate available space
- `int skb_tailroom(struct sk_buff *skb);`
  - Returns the amount of space available for putting data in the buffer
- `int skb_headroom(struct sk_buff *skb);`
  - Returns the amount of space available in front of data
  - This is how many octets you can push onto the buffer
- `void skb_reserve(struct sk_buff *skb, int len);`
  - Increments both data and tail
  - Used to reserve headroom before filling the buffer
- `unsigned char *skb_pull(struct sk_buff *skb, int len);`
  - Removes data from the head of the packet
  - Drivers don't need this
- `int skb_is_nonlinear(struct sk_buff *skb);`
  - Returns a true value if this skb is separated into multiple fragments for scatter/gather I/O
- `int skb_headlen(struct sk_buff *skb);`
  - Returns the length of the first segment of the skb
- `void *kmap_skb_frag(skb_frag_t *frag);` and
- `void kunmap_skb_frag(void *vaddr);`
  - If you must directly access fragments in a nonlinear skb from within the kernel, these functions map and unmap them for you

### MAC Address Resolution

We need a way to associate MAC addresses with the IP number. We are going to focus on the ethernet case with three methods:

1. Address Resolution Protocol (ARP)
2. Ethernet headers without ARP (such as plip)
3. non-Ethernet headers

The usual way to do this is with ARP. This is all handled by the kernel. ether_setup assigns the correct device methods to dev->hard_header and dev->rebuild_header. Normally, the common Ethernet code with ARP takes care of everything. 

Simple point-to-point network interfaces like plip can benefit from using Ethernet headers, while avoiding the overhead of sending ARP packets back and forth. If your device wants to use the usual hardware header without running ARP, you need to override the default dev->hard_header method. snull does it like this:

```c
int snull_header(struct sk_buff *skb, struct net_device *dev,
                 unsigned short type, void *daddr, void *saddr,
                 unsigned int len)
{
     struct ethhdr *eth = (struct ethhdr *)skb_push(skb,ETH_HLEN);
     
     eth->h_proto = htons(type);
     memcpy(eth->h_source, saddr ? saddr : dev->dev_addr, dev->addr_len);
     memcpy(eth->h_dest, daddr ? daddr : dev->dev_addr, dev->addr_len);
     eth->h_dest[ETH_ALEN-1] ^= 0x01; /* dest is us xor 1 */
     return (dev->hard_header_len);
}
```

- This takes the information provided by the kernel and formats it into a standard Ethernet header

To avoid an ethernet header altogether, the receiving function in the driver should correctly set the fields skb->protocol, skb->pkt_type, and skb->mac.raw. Simple point-to-point drivers can have performance benefits by dealing with less overhead of a simple header that does away with most info. Just be careful with these. Ethernet is nice to use as the default unless you have a good reason not to. 

### Custom ioctl Commands

When the ioctl system call is invoked on a socket,  the sock_ioctl function directly invokes a protocol-specific function. Any ioctl command that is not recognized by the protocol layer is passed to the device layer. Device-related ioctl commands accept a third argument from user space: a `struct ifreq*`. Each interface can also define its own ioctl commands (ex: plip can modify timeout values via ioctl). 

### Statistical Information (performance bragging rights)

The last method a driver needs is get_stats. Here is the structure:

```c
struct net_device_stats *snull_stats(struct net_device *dev)
{
     struct snull_priv *priv = netdev_priv(dev);
     return &priv->stats;
}
```

These are the most interesting stat fields:

- `unsigned long rx_packets;` and `unsigned long tx_packets;`
  - The total number of incoming and outgoing packets successfully transferred by the interface
- `unsigned long rx_bytes;` and `unsigned long tx_bytes;`
  - The number of bytes received and transmitted by the interface
- `unsigned long rx_errors;` and `unsigned long tx_errors;`
  - The number of erroneous receptions and transmissions
- `unsigned long rx_dropped;` and `unsigned long tx_dropped;`
  - The number of packets dropped during reception and transmission
  - Happens when there’s no memory available for packet data
- `unsigned long collisions;`
  - The number of collisions due to congestion on the medium
- `unsigned long multicast;`
  - The number of multicast packets received

### Multicast

Behaves like an address group. You assign special hardware addresses to groups of hosts. In the case of Ethernet, a multicast address has the least significant bit of the first address octet set in the destination address, while every device board has that bit clear in its own hardware address.

The tricky part of dealing with host groups and hardware addresses is performed by applications and the kernel, so driver writers can relax and take a vacation during this phase of development. 

Hardware can fall into one of three groups with regards to multicasting:

1. Interfaces that cannot deal with multicast. Boo! They can receive multicast packets only by receiving every packet, thus, potentially overwhelming the operating system with a huge number of “uninteresting” packets.
2. Interfaces that can tell multicast packets from other packets. Let the software determine if the address is interesting for this host. Overhead here is typically okay with a low number of multicast packets.
3. Interfaces that can perform hardware detection of multicast addresses. This is the optimal case for the kernel, because it doesn’t waste processor time dropping “uninteresting” packets received by the interface.

### Kernel Support for Multicasting

Multicast support is made up of a device method, a data structure, and device flags:

- `void (*dev->set_multicast_list)(struct net_device *dev);`
  - Device method called whenever the list of machine addresses associated with the device changes
  - Also called when dev->flags is modified
  - Can be left NULL if you are not interested in using it
- `struct dev_mc_list *dev->mc_list;`
  - A linked list of all the multicast addresses associated with the device
- `int dev->mc_count;`
  - The number of items in the linked list
- `IFF_MULTICAST`
  - Unless the driver sets this flag in dev->flags, the interface won’t be asked to handle multicast packets
  - This is called when dev->flags changes
    - The multicast list may have changed while the interface was not active
- `IFF_ALLMULTI`
  - Flag set in dev->flags by the networking software to tell the driver to retrieve all multicast packets from the network
- `IFF_PROMISC`
  - Flag set in dev->flags when the interface is put into promiscuous mode
  - Why do we have a promiscuous mode in the kernel LOL
  - Every packet should be received by the interface, independent of dev->mc_list

Last thing needed is the definition of `struct dev_mc_list`:

```c
struct dev_mc_list {
     struct dev_mc_list *next;       /* Next address in list */
     __u8               dmi_addr[MAX_ADDR_LEN]; /* Hardware address */
     unsigned char      dmi_addrlen; /* Address length */
     int                dmi_users;   /* Number of users */
     int                dmi_gusers;  /* Number of groups */
};
```


