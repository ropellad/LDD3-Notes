# LDD3 Chapter 13 Notes

## Chapter 13: USB Drivers

Usb has taken over just about everything with peripherals. With USB Type-C and Thunderbolt, it seems like everything is now going to this port. While the current train on dongles most of us have sucks, I think there is a decent future where everything just uses a normal USB-C port, including projectors, mice, thumb drives, headphones, power, and displays. One of the major problems with USB right now is the fragmentation - naming conventions for versions have become confusing, there a ton of old physical port types, and you can't tell the full capabilities of a USB-C port by just looking at (sometimes). This can all be very confusing and here is a decent article that sums up a lot of previous USB nomenclature as well as the current standards:

[What Is USB-C? An Explainer \| PCMag](https://www.pcmag.com/how-to/what-is-usb-c-an-explainer)

At a high level, the universal serial bus is a connection between a host computer and a number of peripheral devices. It was designed to replace many different old and slow ports. I would USB 2.0 Type-A has been the single most successful port of all time, other than maybe a headphone jack. 

Oddly, even though it is called a bus, USB is not laid out like a bus but instead a tree build from several point-to-point links. The links are four-wire cables as followings:

1. Power (red wire)
2. Ground (black wire)
3. Data + (green wire)
4. Data - (white wire)

These wires connect a device to a hub just like a twisted-pair Ethernet. The USB host controller is in charge of asking every USB device connected if it has any data to send. Because of this topology, a USB can never start sending data without being asked to first by the host controller. Having this layout enables a simple plug-and-play system where devices can automatically be configured by the host computer. 

The bus is simple in its master-slave implementation where the host controller polls the various peripheral devices. This might seem like a giant red flag for decent performance, but there some additional interesting features like the ability for a device to request a fixed bandwidth for its data transfers in order to reliably support video and audio I/O. One other cool thing - USB acts merely as a communication channel between the device and host without requiring specific meaning or structure to the data it delivers. That is pretty nice for fully custom communications that could be pretty barebones with low overhead. 

USB protocol has standards that any device of a specific type can follow. If a device follows that set of standards, a custom driver is not necessary (nice!). Some examples of these different types of classes of devices:

- storage devices
- keyboards
- mice
- joysticks
- network devices
- modems

Some examples of things for which there is no standard and a custom driver is needed:

- video devices
- USB-to-serial devices

The Linux kernel supports two main types of USB drivers:

1. Drivers on a host system
    - These control the USB devices that are plugged into it from the host point of view
    - Example: desktop computer
2. Drivers on a device
    - Control how a single device looks to the host computer as a USB device
    - Also called USB gadget drivers to avoid confusion
    - Out of the scope of this book

The USB core provides an interface for USB drivers to use to access and control the USB hardware without having to worry about the types of USB hardware controllers present on the system. 

### USB Device Basics

A USB device is super complex on its own. The good news for us is that Linux provides a subsystem called USB core to handle most of the complexity. USB drivers bind to USB interfaces and not the entire USB device. This makes our lives significantly easier.

#### Endpoints

The most basic kind of USB communication is through something called an end-point. A USB endpoint can carry data in only one direction but can be either way:

- from host to device (called an OUT endpoint)
- from device to host (called an IN endpoint) 

Think of these as one-way pipes. There are four types of endpoints that describe how the data is transmitted:

1. Control
    - Control endpoints are used to allow access to different parts of the USB device. They are commonly used for configuring the device, retrieving info about the device, sending commands to the device, or retrieving status reports about the device. These endpoints are usually small in size. Every USB device has a control endpoint called “endpoint 0” that is used by the USB core to configure the device at insertion time. These transfers are guaranteed by the USB protocol to always have enough reserved bandwidth to make it through to the device.
2. Interrupt
    - Interrupt endpoints transfer small amounts of data at a fixed rate every time the USB host asks the device for data. These endpoints are used in USB keyboards and mice. They are also commonly used to send data to USB devices to control the device, but are not generally used to transfer large amounts of data. These transfers are guaranteed by the USB protocol to always have enough reserved bandwidth to make it through.
3. Bulk
    - Bulk endpoints transfer large amounts of data. These endpoints are usually much larger than interrupt endpoints. They are common for devices that need to transfer any data that must get through with no data loss. These transfers are not guaranteed by the USB protocol to always make it through in a specific amount of time. If there is not enough room on the bus to send the whole BULK packet, it is split up across multiple transfers to or from the device. These endpoints are used in printers, storage, and network devices.
4. Isochronous
    - Isochronous endpoints transfer large amounts of data like bulk, but the data is not always guaranteed to make it through. These endpoints are used in devices that can handle loss of data, and rely more on keeping a constant stream of data flowing. Real-time data collections like audio and video devices use these endpoints.

The control and bulk endpoints are used for asynchronous data transfers when the driver wants to use them. The interrupt and isochronous endpoints are periodic, meaning they are set up to transfer data at fixed times continuously, causing their bandwidth to be reserved by the USB core. USB endpoints are found in the kernel with the structure `struct usb_host_endpoint`. It contains the real endpoint info in another structure called `struct usb_endpoint_descriptor`. This structure contains all the USB-specific data in the exact format that the device specified. The fields of the structure that we care about are:

- bEndpointAddress
  - This is the USB address of this specific endpoint. Also included in this 8-bit value is the direction of the endpoint. The bitmasks USB_DIR_OUT and USB_DIR_IN can be placed against this field to determine if the data for this endpoint is directed to the device or to the host.
- bmAttributes
  - This is the type of endpoint. The bitmask USB_ENDPOINT_XFERTYPE_MASK should be placed against this value in order to determine if the endpoint is of type USB_ENDPOINT_XFER_ISOC, USB_ENDPOINT_XFER_BULK, or USB_ENDPOINT_XFER_INT. These macros define a isochronous, bulk, and interrupt endpoint.
- wMaxPacketSize
  - This is the maximum size in bytes that this endpoint can handle at once. Data will be divided up into wMaxPacketSize chunks if too big. For high-speed devices, this field can be used to support a high-bandwidth mode for the endpoint by using a few extra bits in the upper part of the value.
- bInterval
  - If this endpoint is of type interrupt, this value is the interval setting for the endpoint. The interval is the time between interrupt requests for the endpoint. The value is in milliseconds.

#### Interfaces

USB endpoints are bundled up into interfaces. USB interfaces can only handle one type of a USB logical connection like a mouse, keyboard, or audio stream. Some USB devices have multiple interfaces (those darn multitaskers!) like a USB speaker that consists of two interfaces: a USB keyboard for the buttons and a USB audio stream. This device would need two separate drivers to fully operate. 

USB interfaces can have alternate settings, meaning they have different choices for parameters of the interface. The initial state of an interface is the first setting which is numbered 0. Alternate settings can be used to control individual endpoints in different ways like reserving different amounts of bandwidth for the device. Every device that uses an isochronous endpoint uses alternate settings for the same interface. 

USB interfaces can be found in the kernel with the structure `struct usb_interface`. This structure is what the USB core passes to USB drivers and is what the USB driver is in charge of controlling. Some of the most important fields of the sturcture are:

- struct usb_host_interface *altsetting
  - An array of interface structures containing all of the alternate settings that may be selected for this interface. Each `struct usb_host_interface` consists of a set of endpoint configurations as defined by the `struct usb_host_endpoint` structure described above. 
- unsigned num_altsetting
  - The number of alternate settings pointed to by the `altsetting` pointer
- struct usb_host_interface *cur_altsetting
  - A pointer into the array `altsetting`, denoting the currently active setting for this interface
- int minor
  - If the USB driver bound to this interface uses the USB major number, this variable contains the minor number assigned by the USB core to the interface. This is valid only after a successful call to usb_register_dev

USB drivers do not really need to be aware of the other structure fields in `struct usb_interface`. 

#### Configurations

USB devices are then further bundled up into configurations. A USB device can have multiple configurations and might switch between them in order to change the state of the device. One example: a device that allows new firmware to be downloaded to it might contain multiple configurations to accomplish this. Only one configuration can be enabled at a time. Linux does not handle multiple config devices very well, but they are rare. 

Linux describes USB configurations with the structure `struct usb_host_config` and entire USB devices with the structure `struct usb_device`. USB drivers generally never need to read or write to these, so it is probably okay to not look at them unless you need to.

A USB driver will commonly need to convert data from a given `struct usb_interface` into a `struct usb_device` that the USB core needs for many function calls. To do this, we use the function `interface_to_usbdev`. 

USB devices are complicated for the following reasons:

- Devices usually have one or more configurations
- Configurations often have one or more interfaces
- Interfaces usually have one or more settings
- Interfaces have zero or more endpoints

whew, that's a lot of stuff to remember!

### USB and Sysfs

Because USB devices are so darn complex, their sysfs representations are also complex. The physical device and the individual device interfaces are shown as individual devices since both contain the `struct device` structure. 

The kernel has a special naming convention for USB devices. Let's look at one example:

Device Name: 2-1:1.0
- The first device is a root hub. This is the USB controller, usually contained in a pci device (%% is that still the case today?).
  - This device is called a controller because it controls the entire USB bus connected to it
  - It is a bridge between the PCI bus and the USB bus, as well as being the first device on that bus
  - All root hubs are assigned a unique number by the USB core
  - In this example, the root hub is called usb2
  - There is no limit on the number of root hubs for a single system
- Every device that is on this USB bus takes the number of the root hub as the first number in its name
  - it is followed by the `-` character and then the number of the port that the device is plugged into
  - Since this device is plugged into the first port, a 1 is added to the name after the dash
  - The device name for the main USB device is then 2-1
- With 1 configuration, this device in the tree has more devices down the sysfs path
  - The config number in this case is 1
- The final number in the device is the interface number
  - In our case the interface number is 0
- This makes the final formatted device name 2-1:1.0

The overall naming scheme can be generally stated as:

```c
root_hub-hub_port:config.interface
```

As devices keep going further and further down the USB tree, the hub port number is added to the string following the previous hub port number in the chain. A two-chain device would then look like:

```c
root_hub-hub_port-hub_port:config.interface
```

Sysfs does not expose all of the different parts of a USB device. It stops at the interface level. Alternate configurations of a device are not shown, as well as the endpoints associated with the interfaces. All of this additional info can be found in the usbfs file system mounted in the /proc/bus/usb directory. The file /proc/bus/usb/devices does show all of the same information exposed in sysfs, as well as the alternate configuration and endpoint information for all USB devices that are present in the system.

usbfs also allows user-space programs to directly talk to USB devices. This has allowed a lot of kernel drivers to be moved to user space (this is nice for most people). 

### USB Urbs (so many urbs and spices)

USB code in the Linux kernel communicates with all USB devices using urbs. An urb is a USB request block and is described with the `struct urb` structure in `include/linux/usb.h`.

Urbs are used to send or receive data to or from specific USB endpoints on a specific USB device in an asynchronous manner. A driver can allocate many urbs for a single endpoint or may reuse a single urb for many different endpoints. Every endpoint in a driver can handle a queue of urbs so that multiple urbs can be sent to the same endpoint before the queue is empty. Here is the lifecycle of an urb:

- Created by USB device driver
- Assigned to a specific endpoint of a specific USB device
- Submitted to the USB core by the USB device driver
- Submitted to the specific USB host controller driver for the specified device by the USB core
- Processed by the USB host controller driver that makes a USB transfer to the device
- When the urb is completed, the USB host controller driver notifies the USB device driver

Urbs can be canceled at any time by the driver that submitted them or by the USB core if the device is removed from the system. urbs are dynamically created and contain an internal reference count. This enables them to be automatically freed when the last user of the urb releases it. 

Urbs are good because they permit:

- complex, overlapping communications like streaming
- highest possible data transfer speeds

They are slightly more cumbersome than sending simple messages without regards to speed.

### struct urb (so important it gets its own header!)

Here are the fields of `struct urb` that matter for USB device drivers:

- struct usb_device *dev
  - Pointer to the `struct usb_device` to which this urb is sent. This variable must be initialized by the USB driver before the urb can be sent to the USB core
- unsigned int pipe
  - Endpoint information for the specific `struct usb_device` that this urb is to be sent to. This variable must be initialized by the USB driver before the urb can be sent to the USB core. To set fields of this structure, the driver uses the following functions:
    - unsigned int usb_sndctrlpipe(struct usb_device *dev, unsigned int endpoint)
      - Specifies a control OUT endpoint for the specified USB device with the specified endpoint number
    - unsigned int usb_rcvctrlpipe(struct usb_device *dev, unsigned int endpoint)
      - Specifies a control IN endpoint for the specified USB device with the specified endpoint number
    - unsigned int usb_sndbulkpipe(struct usb_device *dev, unsigned int endpoint)
      - Specifies a bulk OUT endpoint for the specified USB device with the specified endpoint number
    - unsigned int usb_rcvbulkpipe(struct usb_device *dev, unsigned int endpoint)
      - Specifies a bulk IN endpoint for the specified USB device with the specified endpoint number
    - unsigned int usb_sndintpipe(struct usb_device *dev, unsigned int endpoint)
      - Specifies an interrupt OUT endpoint for the specified USB device with the specified endpoint number
    - unsigned int usb_rcvintpipe(struct usb_device *dev, unsigned int endpoint)
      - Specifies an interrupt IN endpoint for the specified USB device with the specified endpoint number
    - unsigned int usb_sndisocpipe(struct usb_device *dev, unsigned int endpoint)
      - Specifies an isochronous OUT endpoint for the specified USB device with the specified endpoint number
    - unsigned int usb_rcvisocpipe(struct usb_device *dev, unsigned int endpoint)
      - Specifies an isochronous IN endpoint for the specified USB device with the specified endpoint number
- unsigned int transfer_flags
  - This variable can be a number of different bit values, depending on what the USB driver wants to happen to the urb. Here are the values you can use:
    - URB_SHORT_NOT_OK
      - specifies that any short read on an IN endpoint that might occur should be treated as an error by the USB core
    - URB_ISO_ASAP
      - If the urb is isochronous, this bit can be set if the driver wants the urb to be scheduled as soon as the bandwidth utilization allows it to be and to set the start_frame variable in the urb at that point
      - If this bit is not set for an isochronous urb, the driver must specify the start_frame value and must be able to recover properly if the transfer cannot start at that moment.
    - URB_NO_TRANSFER_DMA_MAP
      - This should be set when the urb contains a DMA buffer to be transferred. The USB core uses the buffer pointed to by the `transfer_dma` variable and NOT the buffer pointed to by the `transfer_buffer` variable
    - URB_NO_SETUP_DMA_MAP
      - Used for control urbs that have a DMA buffer already set up. If it is set, the USB core uses the buffer pointed to by the `setup_dma` variable instead of the `setup_packet` variable
    - URB_ASYNC_UNLINK
      - If set, the call to usb_unlink_urb for this urb returns almost immediately, and the urb is unlinked in the background
      - Otherwise, the function waits until the urb is completely unlinked and finished before returning
    - URB_NO_FSBR
      - Used by only the UHCI USB Host controller driver and tells it to not try to do Front Side Bus Reclamation logic
      - This bit should generally not be set, because machines with a UHCI host controller create a lot of CPU overhead, and the PCI bus is saturated waiting on a urb that sets this bit
    - URB_ZERO_PACKET
      - When set, a bulk out urb finishes by sending a short packet containing no data= when the data is aligned to an endpoint packet boundary. This is needed by some broken devices to work properly. 
    - URB_NO_INTERRUPT
      - When set, the hardware may not generate an interrupt when the urb is finished
      - The USB core functions use this in order to do DMA buffer transfers
- void *transfer_buffer
  - Pointer to the buffer to be used when sending data to the device
    - for an OUT urb
  - Can also be a pointer to the buffer used when receiving data from the device
    - for an IN urb
  - This buffer must be created with kmalloc
  - For control endpoints, this buffer is for the data stage of the transfer
- dma_addr_t transfer_dma
  - Buffer to be used to transfer data to the USB device using DMA
- int transfer_buffer_length
  - The length of the buffer pointed to by the `transfer_buffer` or the `transfer_dma` variable (only one can be used for a urb)
  - If this is 0, neither transfer buffers are used by the USB core
  - For an OUT endpoint, if the endpoint maximum size is smaller than the value specified in this variable
    - the transfer to the USB device is broken up into smaller chunks in order to properly transfer the data
  - Note: It is much faster to submit a large block of data in one urb and have the USB host controller split it up into smaller pieces than it is to send smaller buffers in consecutive order
- unsigned char *setup_packet
  - Pointer to the setup packet for a control urb
  - Transferred before the data in the transfer buffer
  - Valid only for control urbs
- dma_addr_t setup_dma
  - DMA buffer for the setup packet for a control urb
  - Transferred before the data in the normal transfer buffer
  - Valid only for control urbs
- usb_complete_t complete
  - Pointer to the completion handler function that is called by the USB core when the urb is completely transferred or when an error occurs to the urb
  - The USB driver may inspect the urb, free it, or resubmit it for another transfer
  - The `usb_complete_t` typedef is defined as `typedef void (*usb_complete_t)(struct urb *, struct pt_regs *);`
- void *context
  - Pointer to a chunck of data that can be set by the USB driver
  - It can be used in the completion handler when the urb is returned to the driver
- int actual_length
  - When the urb is finished, this variable is set to the actual length of the data either
    - sent by the urb- for OUT urbs
    - received by the urb- for IN urbs
  - For IN urbs, this must be used instead of the transfer_buffer_length variable
- int status
  - When the urb is finished or being processed by the USB core, this variable is set to the current status of the urb
  - The only time a USB driver can safely access this variable is in the urb completion handler function
    - This restriction is to prevent race conditions that occur
  - For isochronous urbs, a successful value (0) in this variable merely indicates whether the urb has been unlinked
    - To obtain a detailed status on isochronous urbs, the `iso_frame_desc` variables should be checked
  - The valid values for this variable are:
    - 0
      - The urb transfer was successful
    - -ENOENT
      - The urb was stopped by a call to usb_kill_urb
    - -ECONNRESET
      - The urb was unlinked by a call to `usb_unlink_urb`, and the `transfer_flags` variable of the urb was set to `URB_ASYNC_UNLINK`
    - -EINPROGRESS
      - The urb is still beingprocessed by the USB host controllers
      - This indicates a bug in your driver
    - -EPROTO
      - A bitstuff error happened during the transfer OR no response packet was received in time by the hardware
    - -EILSEQ
      - There was a CRC mismatch in the urb transfer
    - -EPIPE
      - The endpoint is now stalled
      - If the endpoint involved is not a control endpoint, this error can be cleared through a call to the function usb_clear_halt
    - -ECOMM
      - Data was received faster duringthe transfer than it could be written to system memory
      - This can only happen with an IN urb
    - -ENOSR
      - Data could not be retrieved from the system memory duringthe transfer fast enough to keep up with the requested USB data rate
      - Only happens with an OUT urb
    - -EOVERFLOW
      - A “babble” error happened to the urb
      - This happens when the endpoint receives more data than the max specified packet size
    - -EREMOTEIO
      - Occurs only if the `URB_SHORT_NOT_OK` flag is set in the urb’s `transfer_flags` variable 
      - It means that the full amount of data requested by the urb was not received
    - -ENODEV
      - The USB device is now gone from the system
    - -EXDEV
      -  The transfer was only partially completed for a isochronous urb
    - -EINVAL
      - Something very bad happened with the urb. Oh no!
    - -ESHUTDOWN
      - There was a severe error with the USB host controller driver
  - In general, the error values of `-EPROTO`, `-EILSEQ`, and `-EOVERFLOW` mean that there are problems with the device, device firmware, or the device cable.
- int start_frame
  - Sets or returns the initial frame number for isochronous transfers to use
- int interval
  - The interval at which the urb is polled
  - Only for interrupt or isochronous urbs
  - Can have units of frames or microframes
    - One frame is 1 millisecond
    - One microframe is 1/8 milliseconds
- int number_of_packets
  - Specifies the number of isochronous transfer buffers to be handled by this urb
  - Only for isochronous urbs
- int error_count
  - Set by the USB core only for isochronous urbs after their completion
  - Specifies the number of isochronous transfers that reported any type of error
- struct usb_iso_packet_descriptor iso_frame_desc[0]
  - This is only for isochronous urbs
  - It is an array of the `struct usb_iso_packet_descriptor` structures that make up this urb
  - It enables a single urb to define a number of isochronous transfers at once
  - It can also collect the transfer status of each individual transfer
  - `struct usb_iso_packet_descriptor` has the following fields:
    - unsigned int offset
      - The offset into the transfer buffer (starting at 0 for the first byte) where this packet’s data is located
    - unsigned int length
      - The length of the transfer buffer for this packet
    - unsigned int actual_length
      - The length of the data received into the transfer buffer for this isochronous packet
    - unsigned int status
      - The status of the individual isochronous transfer of this packet
      - This can have the same return values as the main `struct urb` `status` variable

### Creating and Destroying Urbs

The `struct urb` structure must never be created statically in a driver or within
another structure. This would break the reference counting scheme used by the USB core for urbs. It must be created with a call to the `usb_alloc_urb` function:

```c
struct urb *usb_alloc_urb(int iso_packets, int mem_flags);
```

- `iso_packets` is the number of isochronous packets in this urb
  - Set to 0 if you do not want an isochronous urb
- `mem_flags` is the same type of flagthat is passed to the kmalloc function call to allocate memory from the kernel
- On success it returns a pointer to the urb to the caller
- A return of NULL means something bad happened in USB core

After the urb has been created, it needs to be initialized. We will go over the initialization of several kinds of urbs. When the driver is done with the urb, the driver must call the `usb_free_urb` function. It only has 1 argument:

```c
void usb_free_urb(struct urb *urb);
```

- `struct urb` is the struct you want to release
- The urb is completely gone after a call to this function

#### Interrupt urbs

The function `usb_fill_int_urb` is a helper function to properly initialize a urb to be sent to an interrupt endpoint of a USB device. This is the function:

```c
void usb_fill_int_urb(struct urb *urb, struct usb_device *dev,
                      unsigned int pipe, void *transfer_buffer,
                      int buffer_length, usb_complete_t complete,
                      void *context, int interval);
```

There are a ton of parameters here! More details on each:

- struct urb *urb
  - Pointer to the urb to be initialized
- struct usb_device *dev
  - The USB device to which this urb is to be sent
- unsigned int pipe
  - The specific endpoint of the USB device to which this urb is to be sent
  - Created with `usb_sndintpipe` or `usb_rcvintpipe` functions
- void *transfer_buffer
  - Pointer to the buffer from which outgoing data is taken or into which incoming data is received
  - Use kmalloc to create it!
- int buffer_length
  - Length of the buffer pointed to by the `transfer_buffer` pointer
- usb_complete_t complete
  - Pointer to the completion handler that is called when this urb is completed
- void *context
  - Pointer to the blob that is added to the urb structure for later retrieval by the completion handler function
- int interval
  - Interval at which that this urb should be scheduled
  - Units and details can be found in the `struct urb` description above

#### Bulk urbs

These are similar to interrupt urbs in their initialization. Use the function `usb_fill_bulk_urb` with prototype:

```c
void usb_fill_bulk_urb(struct urb *urb, struct usb_device *dev,
                       unsigned int pipe, void *transfer_buffer,
                       int buffer_length, usb_complete_t complete,
                       void *context);
```

- These arguments are the same as the `usb_fill_int_urb` function
- There is no interval parameter because bulk urbs have no interval value
- `unsigned int pipe` must be initialized with a call to the `usb_sndbulkpipe` or `usb_rcvbulkpipe` function

#### Control urbs

Again, these are similar to bulk urbs, but now we use the function `usb_fill_control_urb` with prototype:

```c
void usb_fill_control_urb(struct urb *urb, struct usb_device *dev,
                          unsigned int pipe, unsigned char *setup_packet,
                          void *transfer_buffer, int buffer_length,
                          usb_complete_t complete, void *context);
```

- The parameters are almost all the same as the `usb_fill_bulk_urb` function 
- The new parameter is called `unsigned char *setup_packet`
  - This must point to the setup packet data to be sent to the endpoint
- `unsigned int pipe` must be initialized with a call to `usb_sndctrlpipe` or `usb_rcvictrlpipe`

Note that this function does not set the `transfer_flags` variable in the urb, so the driver must modify this field. Most drivers do not use this function because it is a lot easier to use synchronous API calls without urbs. 

#### Isochronous urbs

These do not have a nice initializer function like the previous three. They must be initialized by hand. Here is one example:

```c
urb->dev = dev;
urb->context = uvd;
urb->pipe = usb_rcvisocpipe(dev, uvd->video_endp-1);
urb->interval = 1;
urb->transfer_flags = URB_ISO_ASAP;
urb->transfer_buffer = cam->sts_buf[i];
urb->complete = konicawc_isoc_irq;
urb->number_of_packets = FRAMES_PER_DESC;
urb->transfer_buffer_length = FRAMES_PER_DESC;
for (j=0; j < FRAMES_PER_DESC; j++) {
     urb->iso_frame_desc[j].offset = j;
     urb->iso_frame_desc[j].length = 1;
}
```

### Submitting Urbs

Now that the urb has been created and initialized, it needs to be sent to the USB core to be sent to the USB device. This is done with a call to `usb_submit_urb` with the prototype:

```c
int usb_submit_urb(struct urb *urb, int mem_flags);
```

- `urb` is a pointer to the urb to be sent to the device
- `mem_flags` is equivalent to the same parameter that is passed to the kmalloc call
  - used to tell the USB core how to allocate memory buffers at this moment in time
- Once an urb is submitted, don't try to access any fields of the urb structure until the complete function is called

There are three valid values to use for `mem_flags`:

- GFP_ATOMIC
  - This value should be used whenever the following are true:
    - The caller is within an urb completion handler, an interrupt, a bottom half, a tasklet, or a timer callback
    - The caller is holdinga spinlock or rwlock
      - If a semaphore is being held, this value is not necessary
    - The current->state is not TASK_RUNNING
      - The state is always TASK_RUNNING unless the driver has changed the current state itself
- GFP_NOIO
  - Use if the driver is in the block I/O patch
  - It should also be used in the error handling path of all storage-type devices
- GFP_KERNEL
  - This catches everything else that does not fall into the first two categories

### The Urb Completion Callback Handler

If `usb_submit_urb` succeeds and returns 0, the completion handler of the urb is called once when the urb is completed. When called, the USB core is finished with the URB and control of it is now returned to the device driver. 

There are three ways that an urb can be finished AND have the complete function called:

1. The urb is successfully sent to the device, and the device returns the proper acknowledgment
2. Some kind of error happened when sendingor receivingdata from the device
3. The urb was “unlinked” from the USB core, meaning something told USB core to cancel the urb or the device was removed from the system before the urb could run

### Canceling Urbs

To stop a urb that has been submitted to the USB core, the functions `usb_kill_urb` or
`usb_unlink_urb` should be called. Here are the prototypes:

```c
int usb_kill_urb(struct urb *urb);
int usb_unlink_urb(struct urb *urb);
```

- The only argument is a pointer to the urb you want to cancel
- usb_kill_urb stops the urb lifecycle
  - usually used with the device is disconnected from the system
  - found in the disconnect callback
- usb_unlink_urb does not wait for the urb to be fully stopped before returning the caller
  - useful for stoppingthe urb while in an interrupt handler or when a spinlock is held
  - requires that the URB_ASYNC_UNLINK flag value be set in the urb that is being asked to be stopped in order to work properly

### Writing a USB Driver

This is similar to last chapter with the PCI drivers with the overall process. The driver first registers its driver object with the USB subsystem and later uses vendor and device identifiers to tell if the correct hardware has been installed. 

#### WHat Devices Does the Driver Support

The `struct usb_device_id` provides a list of the types of USB devices supported. This list is used by the USB core to decide which driver to give a device to. Hotplug scripts determine which driver to automatically load when a device is plugged into the system.

`struct usb_device_id` is defined with fields:

- `__u16 match_flags`
  - Determines which of the following fields in the structure the device should be matched against
  - This is a bit field defined by the different USB_DEVICE_ID_MATCH_* values specified in the include/linux/mod_devicetable.h file
  - Usually never set directly but is initialized by the USB_DEVICE type macros
- `__u16 idVendor`
  - The USB vendor ID for the device
  - Assigned by the USB forum to its members and cannot be made up by anyone else
  - These numbers feel like an exclusive country club. Fee is about $6k
- `__u16 idProduct`
  - The USB product ID for the device
  - Vendors that have a vendor ID assigned to them can manage their product IDs however they choose to
- `__u16 bcdDevice_lo` and `__u16 bcdDevice_hi`
  - Define the low and high ends of the range of the vendor-assigned product version number
  - The bcdDevice_hi value is inclusive
    - Its value is the number of the highest-numbered device
  - . Both of these values are expressed in binary-coded decimal (BCD) form
- `__u8 bDeviceClass` `__u8 bDeviceSubClass` and `__u8 bDeviceProtocol`
  - Define the class, subclass, and protocol of the device
  - Assigned by the USB forum and defined in the USB specification
  - Specify the behavior for the whole device, including all interfaces on this device
- `__u8 bInterfaceClass` `__u8 bInterfaceSubClass` and `__u8 bInterfaceProtocol`
  - Define the class, subclass, and protocol of the individual interface
  - Assigned by the USB forum and defined in the USB specification
- `kernel_ulong_t driver_info`
  - Holds information that the driver can use to differentiate the different devices from each other in the probe callback function to the USB driver

Similar to PCI devices, a lot of macros can be used to initialize this structure properly:

- USB_DEVICE(vendor, product)
  - Creates a struct usb_device_id that can be used to match only the specified vendor and product ID values
  - Commonly used for USB devices that need a specific driver
- USB_DEVICE_VER(vendor, product, lo, hi)
  - Creates a struct usb_device_id that can be used to match only the specified vendor and product ID values within a version range
- USB_DEVICE_INFO(class, subclass, protocol)
  - Creates a struct usb_device_id that can be used to match a specific class of USB devices
- USB_INTERFACE_INFO(class, subclass, protocol)
  - Creates a struct usb_device_id that can be used to match a specific class of USB interfaces

Let's look at an example for a simple USB driver that controls a single USB device from a single vendor. The `struct usb_device_id` table would look like this:

```c
/* table of devices that work with this driver */
static struct usb_device_id skel_table [ ] = {
 { USB_DEVICE(USB_SKEL_VENDOR_ID, USB_SKEL_PRODUCT_ID) },
 { } /* Terminating entry */
};
MODULE_DEVICE_TABLE (usb, skel_table);
```

- The MODULE_DEVICE_TABLE macro is needed to allow user-space tools to figure out which devices this driver can control. 

#### Registering a USB Driver

All USB drivers must create a `struct usb_driver`. This structure must be filled out by the USB driver and consists of a number of function callbacks and variables to describe the USB driver to the USB core. Here are the fields that must be filled out:

- `struct module *owner`
  - Pointer to the module owner of this driver
  - The USB core uses it to properly reference count this USB driver so that it is not unloaded at inopportune moments
  - The variable should be set to the THIS_MODULE macro
- `const char *name`
  - Pointer to the name of the driver 
  - Must be unique among all USB drivers in the kernel 
  - Normally set to the same name as the module name of the driver
  - Shows up in sysfs under /sys/bus/usb/drivers/ when the driver is in the kernel
- `const struct usb_device_id *id_table`
  - Pointer to the struct usb_device_id table
    - Contains a list of all of the different kinds of USB devices this driver can accept
  - If not set, the probe function callback in the USB driver is never called
  - If you want your driver to be called for every USB device, create an entry that sets only the driver_info field like this:
```c 
static struct usb_device_id usb_ids[ ] = {
     {.driver_info = 42},
     { }
};
```
- `int (*probe) (struct usb_interface *intf, const struct usb_device_id *id)`
  - Pointer to the probe function in the USB driver
  - Called by the USB core when it thinks it has a struct usb_interface that this driver can handle
  - A pointer to the struct usb_device_id that the USB core used to make this decision is also passed to this function
  - If the USB driver claims the struct usb_interface that is passed to it, it should initialize the device properly and return 0
  - If the driver does not want to claim the device, or an error occurs, it should return a negative error value
- `void (*disconnect) (struct usb_interface *intf)`
  - Pointer to the disconnect function in the USB driver
  - Called by the USB core when the struct usb_interface has been removed from the system or when the driver is being unloaded from the USB core

Overall only 5 fields need to be initialized to create a `struct usb_driver` structure

```c
static struct usb_driver skel_driver = {
     .owner = THIS_MODULE,
     .name = "skeleton",
     .id_table = skel_table,
     .probe = skel_probe,
     .disconnect = skel_disconnect,
};
```

There are also callbacks that can be used for ioctl, suspend, and resume, but these are not used very often with USB devices. To register a `struct usb_driver` with the USB core you need to make a call to `usb_register_driver` with a pointer to the usb_driver. This is usually done in module initialization like this:

```c
static int __init usb_skel_init(void)
{
     int result;
     
     /* register this driver with the USB subsystem */
     result = usb_register(&skel_driver);
     if (result)
        err("usb_register failed. Error number %d", result);
        
     return result;
}
```

When you are done playing around with your USB devices and the driver needs to be unloaded, the `struct usb_driver` needs to be unregistered from the kernel. Do this by calling `usb_deregister_driver`. When this call happens, any USB interfaces that were currently bound to this driver are disconnected.

```c
static void __exit usb_skel_exit(void)
{
     /* deregister this driver with the USB subsystem */
     usb_deregister(&skel_driver);
}
```

#### probe and disconnect with USB drivers

In the `struct usb_driver` structure, the driver specifies two functions that the USB core calls at the right times. The first one is called `probe`. This function is called when a device is installed that the USB core thinks this driver should handle. The probe function should perform checks on the info passed to it about the device and decide if the driver is appropriate for that device (feels like a big round of speed dating). The `disconnect` function is called when the driver should no longer control the device for some reason and can do clean-up.

Probe and disconnect are called in the context of the USB hub kernel thread, so it is okay to sleep within them. However, try to do most of your work when the device is opened by a user to minimize probing time. The USB core handles the addition and removal of USB devices within a single thread, so one slow driver can cause USB detection time to slow down for everyone. Don't be the slow one!

In the probe function callback, the USB driver should initialize local structures that it needs to manage the USB device. It should also save info that it needs about the device to the local structure since it is easiest to do this at this time. 

One example: USB drivers usually want to detect the endpoint address and buffer sizes for the device because these are needed to communicate with the device. This code detects the in and out endpoints of bulk type and saves info in a local device structure:

  ```c
  /* set up the endpoint information */
/* use only the first bulk-in and bulk-out endpoints */
iface_desc = interface->cur_altsetting;
for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
     endpoint = &iface_desc->endpoint[i].desc;
     
     if (!dev->bulk_in_endpointAddr &&
         (endpoint->bEndpointAddress & USB_DIR_IN) &&
         ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
             = = USB_ENDPOINT_XFER_BULK)) {
         /* we found a bulk in endpoint */
         buffer_size = endpoint->wMaxPacketSize;
         dev->bulk_in_size = buffer_size;
         dev->bulk_in_endpointAddr = endpoint->bEndpointAddress;
         dev->bulk_in_buffer = kmalloc(buffer_size, GFP_KERNEL);
         if (!dev->bulk_in_buffer) {
             err("Could not allocate bulk_in_buffer");
             goto error;
         }
     }
     
     if (!dev->bulk_out_endpointAddr &&
         !(endpoint->bEndpointAddress & USB_DIR_IN) &&
         ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
         = = USB_ENDPOINT_XFER_BULK)) {
        /* we found a bulk out endpoint */
        dev->bulk_out_endpointAddr = endpoint->bEndpointAddress;
      }
  }
  
if (!(dev->bulk_in_endpointAddr && dev->bulk_out_endpointAddr)) {
     err("Could not find both bulk-in and bulk-out endpoints");
     goto error;
}     
```

This block of code first loops over every endpoint that is present in this interface and assigns a local pointer to the endpoint structure to make it easier to access later. That is what the following lines do:

```c
for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
    endpoint = &iface_desc->endpoint[i].desc;
```

After we have found an endpoint, we check to see if this endpoint's direction is IN. That can be done by checking if the bitmask USB_DIR_IN is contained in the `bEndpointAddress` endpoint variable. If this is true, we then check whether the endpoint is of type BULK or not. We do this by masking off the `bmAttributes` variable with the `USB_ENDPOINT_XFERTYPE_MASK` bitmask and checking if it matches the value `USB_ENDPOINT_XFER_BULK`. Actual lines that do this from the example:

```c
if (!dev->bulk_in_endpointAddr &&
     (endpoint->bEndpointAddress & USB_DIR_IN) &&
     ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
     = = USB_ENDPOINT_XFER_BULK)) {
```

If ALL of these tests are true, the driver knows it has found the proper type of endpoint and can save the information about the endpoint that it will need later to communicate to it over a local strucutre. The example code that does this is:

```c
/* we found a bulk in endpoint */
buffer_size = endpoint->wMaxPacketSize;
dev->bulk_in_size = buffer_size;
dev->bulk_in_endpointAddr = endpoint->bEndpointAddress;
dev->bulk_in_buffer = kmalloc(buffer_size, GFP_KERNEL);
if (!dev->bulk_in_buffer) {
     err("Could not allocate bulk_in_buffer");
     goto error;
}
```

To retrieve local data from the usb_interface structure for future use, the function `usb_set_intfdata` can be used. It was used in the example with:

```c
/* save our data pointer in this interface device */
usb_set_intfdata(interface, dev);
```

To retrieve the data later on, use the function `usb_get_intfdata` as follows:

```c
struct usb_skel *dev;
struct usb_interface *interface;
int subminor;
int retval = 0;

subminor = iminor(inode);

interface = usb_find_interface(&skel_driver, subminor);
if (!interface) {
     err ("%s - error, can't find device for minor %d",
         __FUNCTION__, subminor);
     retval = -ENODEV;
     goto exit;
}

dev = usb_get_intfdata(interface);

if (!dev) {
     retval = -ENODEV;
     goto exit;
}
```

usb_get_intfdata is usually called in the open funciton of a driver and again in the disconnect funciton. 

If the USB driver is not associated with another type of subsystem that handles the user interaction with the device (such as input, tty, video, etc.), the driver can use the USB major number in order to use the traditional char driver interface with user space. Use the `usb_register_dev` in the probe function when you want to register the device with the USB core. Code:

```c
/* we can register the device now, as it is ready */
retval = usb_register_dev(interface, &skel_class);
if (retval) {
     /* something prevented us from registering this driver */
     err("Not able to get a minor for this device.");
     usb_set_intfdata(interface, NULL);
     goto error;
}
```

- usb_register_dev function requires a pointer to a struct usb_interface and a pointer to a struct usb_class_driver
- struct usb_class_driver is used to define a number of different parameters that the USB driver wants the USB core to know when registering for a minor number

`struct usb_class_driver` consists of the following variables:

- `char *name`
  - The name that sysfs uses to describe the device
  - If the number of the device needs to be in the name, the characters %d should be in the name string
- `struct file_operations *fops;`
  - Pointer to the struct file_operations that this driver has defined to use to register as the character device
- `mode_t mode;`
  - The mode for the devfs file to be created for this driver
  - Typical value would be the value S_IRUSR combined with the value S_IWUSR
    - provides only read and write access by the owner of the device file
- `int minor_base;`
  - This is the start of the assigned minor range for this driver
  - Devices associated with this driver are created with unique, increasingminor numbers that start with this value
  - Only 16 devices are allowed to be associated with this driver at any one time unless the CONFIG_USB_DYNAMIC_MINORS configuration option has been enabled for the kernel
    - If this option is enabled, numbers are given on a first come, first serve basis

When a USB device is disconnected, we want to clean up all resources associated with the device. If usb_register_dev was called to allocate a minor number, then we need to use usb_deregister_dev to give that number back to the USB core. 

In the disconnect function, we want to get data previously set with a call to usb_set_intfdata. Then set the data pointer in the struct usb_interface structure to NULL. Do this with:

```c
static void skel_disconnect(struct usb_interface *interface)
{
     struct usb_skel *dev;
     int minor = interface->minor;
     
     /* prevent skel_open( ) from racing skel_disconnect( ) */
     lock_kernel( );
     
     dev = usb_get_intfdata(interface);
     usb_set_intfdata(interface, NULL);
     
     /* give back our minor */
     usb_deregister_dev(interface, &skel_class);
     unlock_kernel( );
     
      /* decrement our usage count */
     kref_put(&dev->kref, skel_delete);
     
     info("USB Skeleton #%d now disconnected", minor);
}
```

- lock_kernel in this code takes the big kernel lock
  - ensures disconnect callback does not enter a race condition with the open call
- Just before disconnect is called for a USB device, all urbs that are currently in transmission for the device are canceled by the USB core automatically!
  - Future urbs sent to the device will fail with -EPIPE

### Submitting and Controlling a Urb  

When a driver needs to send data to the device, a urb must be allocated for transmitting data like this:

```c
urb = usb_alloc_urb(0, GFP_KERNEL);
if (!urb) {
     retval = -ENOMEM;
     goto error;
}
```

After successful allocation, a DMA buffer should also be created to send the data to the device. One way to do this:

```c
buf = usb_buffer_alloc(dev->udev, count, GFP_KERNEL, &urb->transfer_dma);
if (!buf) {
     retval = -ENOMEM;
     goto error;
}
if (copy_from_user(buf, user_buffer, count)) {
     retval = -EFAULT;
     goto error;
}
```

Once the data is copied from the user space into the local buffer, the urb must be initialized before it can be submitted to the USB core with:

```c
/* initialize the urb properly */
usb_fill_bulk_urb(urb, dev->udev,
     usb_sndbulkpipe(dev->udev, dev->bulk_out_endpointAddr),
     buf, count, skel_write_bulk_callback, dev);
urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
```

Now the urb can finally be submitted to the usb core with:

```c
/* send the data out the bulk port */
retval = usb_submit_urb(urb, GFP_KERNEL);
if (retval) {
     err("%s - failed submitting write urb, error %d", __FUNCTION__, retval);
     goto error;
}
```

Once the urb is finished doing its business, the urb callback is called by the USB core. Our example:

```c
static void skel_write_bulk_callback(struct urb *urb, struct pt_regs *regs)
{
     /* sync/async unlink faults aren't errors */
     if (urb->status &&
         !(urb->status = = -ENOENT ||
         urb->status = = -ECONNRESET ||
         urb->status = = -ESHUTDOWN)) {
         dbg("%s - nonzero write bulk status received: %d",
         __FUNCTION__, urb->status);
     }
     
     /* free up our allocated buffer */
     usb_buffer_free(urb->dev, urb->transfer_buffer_length,
         urb->transfer_buffer, urb->transfer_dma);
}
```

- This callback first checks the status of the urb to see if it was successful
- Then, it frees up the allocated buffer used in transmission

### USB Transfers Without Urbs

If you don't want to go through all the hassle of creating the urb structure and initialize and do everything else you don't really want to do, there is a lazy way for transferring USB data. Two functions provide a simpler interface. The first:

#### usb_bulk_msg

This creates a USB bulk urb and sends it to the specified device, then waits for it to complete before returning to the caller. Prototype:

```c
int usb_bulk_msg(struct usb_device *usb_dev, unsigned int pipe,
     void *data, int len, int *actual_length,
     int timeout);
```

Details on the parameters:

- `struct usb_device *usb_dev`
  - A pointer to the USB device to send the bulk message to
- `unsigned int pipe`
  - The specific endpoint of the USB device to which this bulk message is to be sent
  - This value is created with a call to either usb_sndbulkpipe or usb_rcvbulkpipe
- `void *data`
  - A pointer to the data to send to the device if this is an OUT endpoint
  - A pointer to where the data should be placed after being read from the device if an IN endpoint
- `int len`
  - The length of the buffer that is pointed to by the data parameter
- `int *actual_length`
  - A pointer to where the function places the actual number of bytes that have either been transferred to the device or received from the device, depending on the direction of the endpoint
- `int timeout`
  - The amount of time, in jiffies, that should be waited before timingout
  - If this value is 0, the function waits forever for the message to complete
    - Don't wait forever, it never works out ;) 

Function returns 0 on sucess. Negative number if error. Example of a call:

```c
/* do a blocking bulk read to get data from the device */
retval = usb_bulk_msg(dev->udev,
    usb_rcvbulkpipe(dev->udev, dev->bulk_in_endpointAddr),
    dev->bulk_in_buffer,
    min(dev->bulk_in_size, count),
    &count, HZ*10);
    
/* if the read was successful, copy the data to user space */
if (!retval) {
     if (copy_to_user(buffer, dev->bulk_in_buffer, count))
        retval = -EFAULT;
     else
        retval = count;
}
```

- This shows an example bulk read from an IN endpoint

#### usb_control_msg

Works just like the function prior, but it allows a driver to send and recieve USB control messages. Prototype:

```c
int usb_control_msg(struct usb_device *dev, unsigned int pipe,
                     __u8 request, __u8 requesttype,
                     __u16 value, __u16 index,
                     void *data, __u16 size, int timeout);
```

Here is more details on the parameters to use:

- `struct usb_device *dev`
  - A pointer to the USB device to send the control message to
- `unsigned int pipe`
  - The specific endpoint of the USB device that this control message is to be sent to
  - This value is created with a call to either usb_sndctrlpipe or usb_rcvctrlpipe
- `__u8 request`
  - The USB request value for the control message
- `__u8 requesttype`
  - The USB request type value for the control message
- `__u16 value` 
  - The USB message value for the control message
- `__u16 index`
  - The USB message index value for the control message
- `void *data`
  - A pointer to the data to send to the device if this is an OUT endpoint
  - A pointer to where the data should be placed after being read from the device in this is an IN endpoint
- `__u16 size`
  - The size of the buffer that is pointed to by the data parameter
- `int timeout`
  - The amount of time, in jiffies, that should be waited before timingout
  - If set to 0, the function will wait forever for the message to complete

On success, this function returns the number of bytes transferred to or from the device. On failure, it returns a negative number. 

### Other USB Data Functions

A number of helper functions in the USB core can be used to retrieve standard information from all USB devices. These functions cannot be called from within interrupt context or with a spinlock held. The first interesting one is called `usb_get_descriptor`. It retrieves the specified USB descriptor for the specified device as follows:

```c
int usb_get_descriptor(struct usb_device *dev, unsigned char type,
    unsigned char index, void *buf, int size);
```

Parameter details:

- `struct usb_device *usb_dev`
  - A pointer to the USB device that the descriptor should be retrieved from (should look familiar from all other examples)
- `unsigned char type`
  - The descriptor type. This type is described in the USB specification and can be one of the following types:
    - USB_DT_DEVICE
    - USB_DT_CONFIG
    - USB_DT_STRING
    - USB_DT_INTERFACE
    - USB_DT_ENDPOINT
    - USB_DT_DEVICE_QUALIFIER
    - USB_DT_OTHER_SPEED_CONFIG
    - USB_DT_INTERFACE_POWER
    - USB_DT_OTG
    - USB_DT_DEBUG
    - USB_DT_INTERFACE_ASSOCIATION
    - USB_DT_CS_DEVICE
    - USB_DT_CS_CONFIG
    - USB_DT_CS_STRING
    - USB_DT_CS_INTERFACE
    - USB_DT_CS_ENDPOINT
- `unsigned char index`
  - The number of the descriptor that should be retrieved from the device
- `void *buf`
  - A pointer to the buffer to which you copy the descriptor
- `int size`
  - The size of the memory pointed to by the buf variable

This returns the number of bytes transferred on success, and a negative number for an error. 

Whew, that was a lot of notes!









