# LDD3 Chapter 18 Notes

## Chapter 18: TTY Drivers

Chapter goal: Buy a teletypewriter and get it working with the shell like this:

- [Using a 1930 Teletype as a Linux Terminal](https://www.youtube.com/watch?v=2XLZ4Z8LpEE)

Just kidding. 

Note on this chapter: It uses the tiny tty driver. There is a modern version written here:

- [GitHub - martinezjavier/ldd3: Linux Device Drivers 3 examples updated to work in recent kernels](https://github.com/martinezjavier/ldd3)
- Note: There is one fix needed to get theses to (kinda) work (visible in my directory of these notes)
- I had trouble with unloading and reloading tiny_ttl.ko, but loading the first time worked
- I also wrote an unloading function for tiny_serial.ko so that you can unload it and reload it. Previously there was no way to unload it
```c
// Replace this line:
#define TINY_TTY_MAJOR      240    /* experimental range */

//with:
#define TINY_TTY_MAJOR      0    /* dynamically determine (max is 511) */

// Most likely, 240 is already in use as a major number and the module will not be able 
// to be inserted. By setting this value to 0, the kernel is able to dynamically 
// determine a good major number at runtime. The rest should work in the driver. 
```

TTY devices are typically any serial port style device. Some examples:

- Serial Ports
- USB-to-serial-port converters
- Some modems

tty virtual devices support virtual consoles. These are used for things like login from a keyboard, network, or xterm session.

The Linux tty core is responsible for controlling both the flow of data across a tty device and the format of the data. tty drivers thus only need to focus on handling the data to and from the hardware without paying much attention to how to control the interaction with user space in a consistent way. Data flow control is done with different line disciplines that can be virtually plugged into any tty device. There are a few different tty line discipline drivers to choose from. 

There are three different types of tty drivers:

1. console
2. serial port
3. pty

1 and 3 have already been written, and any new drivers using the tty core to interact with the user and the system are serial port drivers. To determine what kind of tty drivers are currently loaded in the kernel and what tty devices are present, look at the /proc/tty/drivers file. It shows driver name, node name, major number, range of minors used, and the type of tty driver. Here is a sample output from my machine:

```shell
$ cat /proc/tty/drivers

/dev/tty             /dev/tty        5       0 system:/dev/tty
/dev/console         /dev/console    5       1 system:console
/dev/ptmx            /dev/ptmx       5       2 system
/dev/vc/0            /dev/vc/0       4       0 system:vtmaster
rfcomm               /dev/rfcomm   216 0-255 serial
dbc_serial           /dev/ttyDBC   242       0 serial
dbc_serial           /dev/ttyDBC   243       0 serial
ttyprintk            /dev/ttyprintk   5       3 console
max310x              /dev/ttyMAX   204 209-224 serial
serial               /dev/ttyS       4 64-111 serial
pty_slave            /dev/pts      136 0-1048575 pty:slave
pty_master           /dev/ptm      128 0-1048575 pty:master
unknown              /dev/tty        4 1-63 console
```

/proc/tty/driver/ contains individual files for some of the tty drivers if they implement that functionality. Info on how to create a file for this directory is explained later. All tty devices registered and present in the kernel have their own subdirectory under /sys/class/tty. Within that subdirectory there is a dev file that contains the major and minor number assigned to that tty device. Here is my tree:

```shell
$ tree /sys/class/tty

/sys/class/tty
├── console -> ../../devices/virtual/tty/console
├── ptmx -> ../../devices/virtual/tty/ptmx
├── tty -> ../../devices/virtual/tty/tty
├── tty0 -> ../../devices/virtual/tty/tty0
├── tty1 -> ../../devices/virtual/tty/tty1
├── tty10 -> ../../devices/virtual/tty/tty10
├── tty11 -> ../../devices/virtual/tty/tty11
├── tty12 -> ../../devices/virtual/tty/tty12
├── tty13 -> ../../devices/virtual/tty/tty13
├── tty14 -> ../../devices/virtual/tty/tty14
├── tty15 -> ../../devices/virtual/tty/tty15
├── tty16 -> ../../devices/virtual/tty/tty16
├── tty17 -> ../../devices/virtual/tty/tty17
├── tty18 -> ../../devices/virtual/tty/tty18
├── tty19 -> ../../devices/virtual/tty/tty19
├── tty2 -> ../../devices/virtual/tty/tty2
├── tty20 -> ../../devices/virtual/tty/tty20
...

$ tree /sys/class/tty/tty0

/sys/class/tty/tty0
├── active
├── dev
├── power
│   ├── async
│   ├── autosuspend_delay_ms
│   ├── control
│   ├── runtime_active_kids
│   ├── runtime_active_time
│   ├── runtime_enabled
│   ├── runtime_status
│   ├── runtime_suspended_time
│   └── runtime_usage
├── subsystem -> ../../../../class/tty
└── uevent

2 directories, 12 files
```

### Simple TTY Driver

Let's create a simple TTY driver that we can load, write to, read from, and unload. The main structure is the `struct tty_driver`. It is used to register and unregister a tty driver with the tty core. You can find it in `<linux/tty_driver.h>`.

To create the `struct tty_driver`, the function `alloc_tty_driver` should be called with the number of tty devices this driver supports as the parameter. We do this with:

```c
/* allocate the tty driver */
tiny_tty_driver = alloc_tty_driver(TINY_TTY_MINORS);
if (!tiny_tty_driver)
    return -ENOMEM;
```

This structure contains many fields, but not all of them need to be initialized for a basic driver. Here is a bare-minimum working example:

```c
static struct tty_operations serial_ops = {
     .open = tiny_open,
     .close = tiny_close,
     .write = tiny_write,
     .write_room = tiny_write_room,
     .set_termios = tiny_set_termios,
};

 /* initialize the tty driver */
 tiny_tty_driver->owner = THIS_MODULE;
 tiny_tty_driver->driver_name = "tiny_tty";
 tiny_tty_driver->name = "ttty";
 tiny_tty_driver->devfs_name = "tts/ttty%d";
 tiny_tty_driver->major = TINY_TTY_MAJOR,
 tiny_tty_driver->type = TTY_DRIVER_TYPE_SERIAL,
 tiny_tty_driver->subtype = SERIAL_TYPE_NORMAL,
 tiny_tty_driver->flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_NO_DEVFS,
 tiny_tty_driver->init_termios = tty_std_termios;
 tiny_tty_driver->init_termios.c_cflag = B9600 | CS8 | CREAD | HUPCL | CLOCAL;
 tty_set_operations(tiny_tty_driver, &serial_ops);
```

To register this driver with the tty core, tty_driver must be passed to the tty_register_driver function like this:

```c
/* register the tty driver */
retval = tty_register_driver(tiny_tty_driver);
if (retval) {
     printk(KERN_ERR "failed to register tiny tty driver");
     put_tty_driver(tiny_tty_driver);
     return retval;
}
```

After registering itself, the driver registers the devices it controls through the tty_ register_device function. This function has three arguments:

1. A pointer to the struct tty_driver that the device belongs to
2. The minor number of the device
3. A pointer to the struct device that this tty device is bound to
   - If the tty device is not bound to any struct device, this argument can be set to NULL

The driver we are creating registers all the tty devices at once with:

```c
for (i = 0; i < TINY_TTY_MINORS; ++i)
 tty_register_device(tiny_tty_driver, i, NULL);
```

To unregister everything, start by unregistering each device, followed by the driver:

```c
for (i = 0; i < TINY_TTY_MINORS; ++i)
    tty_unregister_device(tiny_tty_driver, i);
tty_unregister_driver(tiny_tty_driver);
```

### `struct termios`

The `init_termios` variable in the `struct tty_driver` is a `struct termios`. It is used to provide a sane set of line settings if the port is used before it is initialized by a user. The driver initializes the variable with a standard set of values, which is copied from the tty_std_termios variable. tty_std_termios is defined in the tty core as:

```c
struct termios tty_std_termios = {
     .c_iflag = ICRNL | IXON,
     .c_oflag = OPOST | ONLCR,
     .c_cflag = B38400 | CS8 | CREAD | HUPCL,
     .c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK |
     ECHOCTL | ECHOKE | IEXTEN,
     .c_cc = INIT_C_CC
};
```

This struct is used to hold all the current line settings for a specific port on the tty device. These line settings control the current baud rate, data size, data flow settings, and many other values. Fields of this structure:

- `tcflag_t c_iflag;`
  - The input mode flags
- `tcflag_t c_oflag;`
  - The output mode flags
- `tcflag_t c_cflag;`
  - The control mode flags
- `tcflag_t c_lflag;`
  - The local mode flags
- `cc_t c_line;`
  - The line discipline type
- `cc_t c_cc[NCCS];`
  - An array of control characters

The mode flags are defined as a large bitfield. The terminal, of course, provides a useful set of macros to get at the different bits. They are defined in `include/linux/tty.h`. 

The driver_name and name fields look very similar, yet are used for different purposes:

- `driver_name` should be set to something short, descriptive, and unique among all tty drivers
  - It shows up in the /proc/tty/drivers file to describe the driver to the user and in the sysfs tty class directory of tty drivers currently loaded
- `name` is used to define a name for the individual tty nodes assigned to this tty driver in the /dev tree
  - Used to create a tty device by appending the number of the tty device being used at the end of the string
  - Also used to create the device name in the sysfs /sys/class/tty/ directory
  - The string is also displayed in the /proc/tty/drivers file
  - The kernel sets the name field to tts/ if devfs is enabled and ttyS if it is not

/proc/tty/drivers shows all of the currently registered tty drivers. With tiny_tty driver registered in the kernel:

```shell
$ cat /proc/tty/drivers

tiny_tty             /dev/ttty     237       0 serial
```

/sys/class/tty then looks like this:

```shell
$ tree /sys/class/tty/ttty*

/sys/class/tty/ttty0
├── dev
├── power
│   ├── async
│   ├── autosuspend_delay_ms
│   ├── control
│   ├── runtime_active_kids
│   ├── runtime_active_time
│   ├── runtime_enabled
│   ├── runtime_status
│   ├── runtime_suspended_time
│   └── runtime_usage
├── subsystem -> ../../../../class/tty
└── uevent

2 directories, 11 files
```

And finally the dev part of class:

```shell
$ cat /sys/class/tty/ttty0/dev 

237:0
```

The major variable describes what the major number for the driver is. The type and subtype variables declare what type of tty driver this driver is. Our example is a serial driver of normal type. The only other type would be a callout type. Callout is old and no longer used. The flags variable is used by the driver and core to indicate the current state of the driver and what kind of tty driver it is. Three bits in the flags variable can be set by the driver:

- `TTY_DRIVER_RESET_TERMIOS`
  - This flag states that the tty core resets the termios setting whenever the last process has closed the device
  - Useful for the console and pty drivers
- `TTY_DRIVER_REAL_RAW`
  - This flag states that the tty driver guarantees to send notifications of parity or break characters up-to-the-line discipline
  - Allows the line discipline to process received characters in a much quicker manner
    - Eliminates need to inspect every character received from the tty driver
  - Typically set for all tty drivers for speed reasons
- `TTY_DRIVER_NO_DEVFS`
  - This flag states that when the call to tty_register_driver is made, the tty core does not create any devfs entries for the tty devices
  - Useful for any driver that dynamically creates and destroys the minor devices
  - USB to serial drivers use this

### tty_driver Function Pointers

tiny_tty declares four function pointers:

1. open 
2. close
3. write
4. write_room

#### open and close

The open function is called by the tty core when a user calls open on the device node the tty driver is assigned to. The tty core calls this with a pointer to the tty_struct structure assigned to this device as well as a file pointer. 

When open is called, the tty driver is expected to either:

- Save some data within the tty_struct variable that is passed to it OR
- Save the data within a static array that can be referenced based on the minor number of the port

The tiny_tty driver saves a pointer within the tty structure:

```c
static int tiny_open(struct tty_struct *tty, struct file *file)
{
     struct tiny_serial *tiny;
     struct timer_list *timer;
     int index;
     
     /* initialize the pointer in case something fails */
     tty->driver_data = NULL;
     
     /* get the serial object associated with this tty pointer */
     index = tty->index;
     tiny = tiny_table[index];
     if (tiny = = NULL) {
         /* first time accessing this device, let's create it */
         tiny = kmalloc(sizeof(*tiny), GFP_KERNEL);
         if (!tiny)
            return -ENOMEM;
            
         init_MUTEX(&tiny->sem);
         tiny->open_count = 0;
         tiny->timer = NULL;
         
         tiny_table[index] = tiny;
     }
     
     down(&tiny->sem);
     
     /* save our structure within the tty structure */
     tty->driver_data = tiny;
     tiny->tty = tty;
```

- tiny_serial structure is saved within the tty structure
  - This allows tiny_write, tiny_write_room, and tiny_close functions to retrieve the tiny_serial structure and manipulate it

The tiny_serial structure is defined as:

```c
struct tiny_serial {
     struct tty_struct *tty; /* pointer to the tty for this device */
     int open_count; /* number of times this port has been opened */
     struct semaphore sem; /* locks this structure */
     struct timer_list *timer;
};
```

- `open_count` is initialized to zero in the open call the first time the port is opened
- A count of how many times the port has been opened or closed must be kept
  - This is because the open and close functions of a tty driver can be called multiple times for the same device for multi-process interaction
- When the port is closed for the last time, any needed hardware shutdown and memory cleanup can be done

Close needs to be written to consider that it can be called multiple times when multiple processes are using it. It needs to decide if it can really be shut down!

We do this with:

```c
static void do_close(struct tiny_serial *tiny)
{
     down(&tiny->sem);
     
     if (!tiny->open_count) {
         /* port was never opened */
         goto exit;
     }
     
     --tiny->open_count;
     if (tiny->open_count <= 0) {
         /* The port is being closed by the last user. */
         /* Do any hardware specific stuff here */
         /* shut down our timer */
         del_timer(tiny->timer);
     }
  exit:
     up(&tiny->sem);
}
  
static void tiny_close(struct tty_struct *tty, struct file *file)
{
     struct tiny_serial *tiny = tty->driver_data;
     if (tiny)
        do_close(tiny);
}
```

### Flow of Data

The write function call is called by the user when there is data to be sent to the hardware. tty core receives the call and passes the data on to the tty driver's write function. The core also informs the driver of the size of the data.

The write function needs to return the number of characters written so the program can verify that this actually happened. It is easier to do the re-writing from user space side, and you should only return an error as a negative error code to prevent confusion. 

Write can be called from both interrupt context and user context. The tty driver should thus not call any functions that might sleep when in interrupt context. If you want to sleep, check first whether the driver is in interrupt context by calling in_interrupt. 

tiny_tty does not connect to any real hardware, so the write function simply records in the kernel log what data was supposed to be written:

```c
static int tiny_write(struct tty_struct *tty,
                      const unsigned char *buffer, int count)
{
     struct tiny_serial *tiny = tty->driver_data;
     int i;
     int retval = -EINVAL;
     
     if (!tiny)
        return -ENODEV;
        
     down(&tiny->sem);
     
     if (!tiny->open_count)
         /* port was not opened */
         goto exit;
         
     /* fake sending the data out a hardware port by
     * writing it to the kernel debug log.
     */
     printk(KERN_DEBUG "%s - ", __FUNCTION__);
     for (i = 0; i < count; ++i)
        printk("%02x ", buffer[i]);
     printk("\n");
     
  exit:
     up(&tiny->sem);
     return retval;
}
```

- The write function can be called when the tty subsystem itself needs to send some data out the tty device
- Try to implement the write function so it always writes at least one byte before returning
  - This will make your stuff work better, just trust me on this one

The write_room function is called when the tty core wants to know how much room in the write buffer the tty driver has available. This number will flutuate as characters empty out of the write buffers and the write function is called, adding more characters to the buffer:

```c
static int tiny_write_room(struct tty_struct *tty)
{
     struct tiny_serial *tiny = tty->driver_data;
     int room = -EINVAL;
     
     if (!tiny)
        return -ENODEV;
        
     down(&tiny->sem);
     
     if (!tiny->open_count) {
         /* port was not opened */
         goto exit;
     }
     
     /* calculate how much room is left in the device */
     room = 255;
     
exit:
     up(&tiny->sem);
     return room;
}
```

### Other Buffering Functions

The chars_in_buffer function in the tty_driver structure is reccomended but not required. It is called when the tty core wants to know how many characters are still remaining in the tty driver's write buffer being sent out. If the driver can store characters before it sends them out to the hardware, it should implement this function in order for the tty core to be able to determine if all of the data in the driver has drained out. 

If the driver can buffer data before sending it out, there are three useful functions that can flush data that the driver is holding onto. They are:

- flush_chars
  - Called when the tty core wants the tty driver to start sending these characters out to the hardware, if it hasn’t already started
- wait_until_sent
  - Must wait until all of the characters are sent before returning to the tty core or until the passed in timeout value has expired
  - Typically combined with flush_chars
- flush_buffer
  - Flush all of the data still in its write buffers out of memory
  - Remaining data in the buffer is lost

### Why no `read` for tty?

tty_core does not provide a read function. tty is responsible for sending any data received from the hardware to the tty core when it is received. tty core buffers the data until it is asked for by the user. 

The tty core buffers the data received by the tty drivers in a structure called `struct tty_flip_buffer`. A flip buffer is a data structure with two main data arrays:

- Data being received from the tty device is stored in the first array
  - When the array is full, the user is notified that data is available to read
- When the user is reading data from the first array, any new incoming data is being stored in the second array
- This process repeats with data and flipping between the two arrays

To push data to the user, a call to tty_flip_buffer_push is made. Whenever data is added to the flip buffer OR when the flip buffer is full, the tty driver must call tty_flip_buffer_push. If you need to be really fast, the tty->low_latency flag should be set. This makes things happen immediately when called. Otherwise you just get scheduled. :( 

### TTY Line Settings
 
Termios user-space library function calls or direct ioctl calls on a tty device can be used to change/retrieve the line settings. The tty core converts both of these interfaces into tty driver function callbacks and ioctl calls. 

#### set_termios

This callback needs to determine which line settings it is being asked to change, and then make those changes in the tty device. First, it determines whether anything has to be changed:

```c
unsigned int cflag;

cflag = tty->termios->c_cflag;

/* check that they really want us to change something */
if (old_termios) {
     if ((cflag = = old_termios->c_cflag) &&
         (RELEVANT_IFLAG(tty->termios->c_iflag) = =
          RELEVANT_IFLAG(old_termios->c_iflag))) {
         printk(KERN_DEBUG " - nothing to change...\n");
         return;
     }
}

// The RELEVANT_IFLAG macro is defined as:
#define RELEVANT_IFLAG(iflag) ((iflag) & (IGNBRK|BRKINT|IGNPAR|PARMRK|INPCK))
// This is used to mask off the important bits of the cflags variable
```

To look at the requested byte size, the CSIZE bitmask can be used to separate the proper bits from the cflag variable. Default is 8 bits:

```c
/* get the byte size */
switch (cflag & CSIZE) {
     case CS5:
         printk(KERN_DEBUG " - data bits = 5\n");
         break;
     case CS6:
         printk(KERN_DEBUG " - data bits = 6\n");
         break;
     case CS7:
         printk(KERN_DEBUG " - data bits = 7\n");
         break;
     default:
         case CS8:
         printk(KERN_DEBUG " - data bits = 8\n");
     break;
}
```

The parity value can be checked with:

```c
/* determine the parity */
if (cflag & PARENB) // PARENB is the bitmask
    if (cflag & PARODD)
        printk(KERN_DEBUG " - parity = odd\n");
    else
        printk(KERN_DEBUG " - parity = even\n");
else
    printk(KERN_DEBUG " - parity = none\n");
```

You can also determine the stop bits with:

```c
/* figure out the stop bits requested */
if (cflag & CSTOPB) //CSTOPB is the bit mask
    printk(KERN_DEBUG " - stop bits = 2\n");
else
    printk(KERN_DEBUG " - stop bits = 1\n");
```

To determine if the flow control is hardware flow control or software flow control:

```c
/* figure out the hardware flow control settings */
if (cflag & CRTSCTS) //CRTSCTS is the bit mask
    printk(KERN_DEBUG " - RTS/CTS is enabled\n");
else
    printk(KERN_DEBUG " - RTS/CTS is disabled\n");
```

Now, a more complicated operation is to determine which mode of software flow control is being used:

```c
/* determine software flow control */
/* if we are implementing XON/XOFF, set the start and
 * stop character in the device */
if (I_IXOFF(tty) || I_IXON(tty)) {
     unsigned char stop_char = STOP_CHAR(tty);
     unsigned char start_char = START_CHAR(tty);
     
     /* if we are implementing INBOUND XON/XOFF */
     if (I_IXOFF(tty))
        printk(KERN_DEBUG " - INBOUND XON/XOFF is enabled, "
            "XON = %2x, XOFF = %2x", start_char, stop_char);
     else
        printk(KERN_DEBUG" - INBOUND XON/XOFF is disabled");
        
     /* if we are implementing OUTBOUND XON/XOFF */
     if (I_IXON(tty))
        printk(KERN_DEBUG" - OUTBOUND XON/XOFF is enabled, "
            "XON = %2x, XOFF = %2x", start_char, stop_char);
     else
        printk(KERN_DEBUG" - OUTBOUND XON/XOFF is disabled");
}
```

And finally, get the baud rates! (much easier!)

```c
/* get the baud rate wanted */
printk(KERN_DEBUG " - baud rate = %d", tty_get_baud_rate(tty));
```

### tiocmget and tiocmset

These two tty ioctl callbacks are used to get and set different control line settings.

- tiocmget is called by the tty core when the core wants to know the current physical values of the control lines of a specific tty device
  - Usually done to retrieve the values of the DTR and RTSlines of a serial port
  - If you can't access these values on a device, a local copy should be kept

Here is how a local copy can be used:

```c
static int tiny_tiocmget(struct tty_struct *tty, struct file *file)
{
     struct tiny_serial *tiny = tty->driver_data;
     
     unsigned int result = 0;
     unsigned int msr = tiny->msr;
     unsigned int mcr = tiny->mcr;
     
     result = ((mcr & MCR_DTR) ? TIOCM_DTR : 0)   | /* DTR is set */
              ((mcr & MCR_RTS) ? TIOCM_RTS : 0)   | /* RTS is set */
              ((mcr & MCR_LOOP) ? TIOCM_LOOP : 0) | /* LOOP is set */
              ((msr & MSR_CTS) ? TIOCM_CTS : 0)   | /* CTS is set */
              ((msr & MSR_CD) ? TIOCM_CAR : 0)    | /* Carrier detect is set*/
              ((msr & MSR_RI) ? TIOCM_RI : 0)     | /* Ring Indicator is set */
              ((msr & MSR_DSR) ? TIOCM_DSR : 0); /* DSR is set */
     return result;
}
```

- tiocmset is called by the tty core when the core wants to set the values of the control lines of a specific tty device
  - Core tells the tty driver what values to set and what to clear, by passing them in two variables that contain bitmasks:
    - set 
    - clear
  - set and clear are never called at the same time, so it does not matter which comes first

```c
static int tiny_tiocmset(struct tty_struct *tty, struct file *file,
                         unsigned int set, unsigned int clear)
{
     struct tiny_serial *tiny = tty->driver_data;
     unsigned int mcr = tiny->mcr;
     
     if (set & TIOCM_RTS)
        mcr |= MCR_RTS;
     if (set & TIOCM_DTR)
        mcr |= MCR_RTS;
        
     if (clear & TIOCM_RTS)
        mcr &= ~MCR_RTS;
     if (clear & TIOCM_DTR)
        mcr &= ~MCR_RTS;
        
     /* set the new MCR value in the device */
     tiny->mcr = mcr;
     return 0;
}
```

### ioctl Stuff for tty

The 2.6 kernel defines about 70 different tty ioctls that can be be sent to a tty driver (that's a crap ton). You are most likely only going to use a few. Here are some of the most popular:

- `TIOCSERGETLSR`
  - Gets the value of this tty device’s line status register (LSR)
- `TIOCGSERIAL`
  - Gets the serial line information
  - You can get a lot of data about this. Here is one implementation:

```c
static int tiny_ioctl(struct tty_struct *tty, struct file *file,
 unsigned int cmd, unsigned long arg)
{
     struct tiny_serial *tiny = tty->driver_data;
     if (cmd = = TIOCGSERIAL) {
         struct serial_struct tmp;
         if (!arg)
            return -EFAULT;
         memset(&tmp, 0, sizeof(tmp));
         tmp.type = tiny->serial.type;
         tmp.line = tiny->serial.line;
         tmp.port = tiny->serial.port;
         tmp.irq = tiny->serial.irq;
         tmp.flags = ASYNC_SKIP_TEST | ASYNC_AUTO_IRQ;
         tmp.xmit_fifo_size = tiny->serial.xmit_fifo_size;
         tmp.baud_base = tiny->serial.baud_base;
         tmp.close_delay = 5*HZ;
         tmp.closing_wait = 30*HZ;
         tmp.custom_divisor = tiny->serial.custom_divisor;
         tmp.hub6 = tiny->serial.hub6;
         tmp.io_type = tiny->serial.io_type;
         if (copy_to_user((void __user *)arg, &tmp, sizeof(tmp)))
         return -EFAULT;
         return 0;
     }
     return -ENOIOCTLCMD;
}
```

- `TIOCSSERIAL`
  - Sets the serial line information
  - Allows the user to set the serial line status of the tty device all at once
  - You can still work properly without this call
- `TIOCMIWAIT`
  - Waits for MSR change
  - The arg parameter contains the type of event that the user is waiting for
  - Commonly used to wait until a status line changes
    - Thus indicating more data is ready to be sent to the device
  - Use a wait queue with this to avoid sleep issues
  - Example:

```c
static int tiny_ioctl(struct tty_struct *tty, struct file *file,
 unsigned int cmd, unsigned long arg)
{
     struct tiny_serial *tiny = tty->driver_data;
     if (cmd = = TIOCMIWAIT) {
         DECLARE_WAITQUEUE(wait, current);
         struct async_icount cnow;
         struct async_icount cprev;
         cprev = tiny->icount;
         while (1) {
             add_wait_queue(&tiny->wait, &wait);
             set_current_state(TASK_INTERRUPTIBLE);
             schedule( );
             remove_wait_queue(&tiny->wait, &wait);
             /* see if a signal woke us up */
             if (signal_pending(current))
                return -ERESTARTSYS;
             cnow = tiny->icount;
             if (cnow.rng = = cprev.rng && cnow.dsr = = cprev.dsr &&
                 cnow.dcd = = cprev.dcd && cnow.cts = = cprev.cts)
                 return -EIO; /* no change => error */
             if (((arg & TIOCM_RNG) && (cnow.rng != cprev.rng)) ||
                 ((arg & TIOCM_DSR) && (cnow.dsr != cprev.dsr)) ||
                 ((arg & TIOCM_CD) && (cnow.dcd != cprev.dcd)) ||
                 ((arg & TIOCM_CTS) && (cnow.cts != cprev.cts)) ) {
                 return 0;
             }
             cprev = cnow;
         }
     }
     return -ENOIOCTLCMD;
}
```

- `TIOCGICOUNT`
  - Gets interrupt counts
  - Called when the user wants to know how many serial line interrupts have happened
  - This ioctl call passes the kernel a pointer to a structure serial_icounter_struct, which should be filled by the tty driver
  - Often used with TIOCMIWAIT
  - Example:

```c
static int tiny_ioctl(struct tty_struct *tty, struct file *file,
                      unsigned int cmd, unsigned long arg)
{
     struct tiny_serial *tiny = tty->driver_data;
     if (cmd = = TIOCGICOUNT) {
         struct async_icount cnow = tiny->icount;
         struct serial_icounter_struct icount;
         icount.cts = cnow.cts;
         icount.dsr = cnow.dsr;
         icount.rng = cnow.rng;
         icount.dcd = cnow.dcd;
         icount.rx = cnow.rx;
         icount.tx = cnow.tx;
         icount.frame = cnow.frame;
         icount.overrun = cnow.overrun;
         icount.parity = cnow.parity;
         icount.brk = cnow.brk;
         icount.buf_overrun = cnow.buf_overrun;
         if (copy_to_user((void __user *)arg, &icount, sizeof(icount)))
            return -EFAULT;
         return 0;
     }
     return -ENOIOCTLCMD;
}
```

### proc and sysfs Handling of TTY Devices

The tty core provides a (supposedly) very easy way for any tty driver to maintain a file in the /proc/tty/driver directory. (ironically, this is what I had trouble fixing in the sample driver). If the driver defines the read_proc or write_proc functions, this file is created. Any read or write call on this file is sent to the driver. I think this aspect of the kernel has changed a bit since the book was published. 

The tty core handles all of the sysfs directory and device creation when the tty driver is registered or when the individual tty devices are created. This depends on the TTY_DRIVER_NO_DEVFS flag in the struct tty_driver. The individual directory always contains the dev file so that user space can determine the major and minor number of the device. It also contains the device and driver symlink.

### The Rest of This Chapter

The remainder of this chapter is a HUGE list of different fields for three things:

1. The `tty_driver` structure used to register a tty driver with the tty core
2. The `tty_operations` structure that contains all of the function callbacks that can be set by a tty driver and called by the tty core
3. The `tty_struct` variable is used by the tty core to keep the current state of a specific tty port

What follows is about 4 pages worth of fields to enter. If you are making a tty driver look back to these pages (567-573) in the book for a lot more detail. I am going to omit them from these notes because I don't think they provide a great deal of intuition on how tty devices work. 

That concludes the notes for this book. If you actually read through all of these you are crazy. 

If you feel lost now that you don't have LDD3 to read everyday anymore, consider the following books to round out your Linux skills:

1. The Linux Programming Interface by Michael Kerrisk
2. Linux Driver Development for Embedded Processors by Alberto Liberal de los Ríos
3. The Little Book of Semaphores by Allen B. Downey
4. Operating Systems: Three Easy Pieces by Remzi H. Arpaci-Dusseau and Andrea C. Arpaci-Dusseau
5. Linux System Programming by Robert Love
6. Learning the bash Shell: Unix Shell Programming by Cameron Newham
7. C Programming Language by Brian Kernighan and Dennis Ritchie (my fav book)
