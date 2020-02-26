# LDD3 Chapter 10 Notes

## Chapter 10: Interrupt Handling

Chapter goal: Understand how interrupts works with devices. This chapter shows a picture of a horse carriage exploding all over an old town. This is probably an accurate description of what is actually happening in a modern system when there are too many interrupts causing confusion. Avoid interrupts if you can, but there are some applications that require interrupts (such as encoders on microcontrollers). 

An interrupt is simply a signal that hardware can send when it wants a processor's attention. Linux handles interrupts much like signals from user space. A driver must register a handler for its device's interrupts and handle them properly when they arrive. 

This chapter uses the parallel port again (boo!) and we will be looking at the `short` module from chapter 9. 

### Prepping the Parallel Port

The parallel port can trigger interrupts. This is used by the printer to notify the lp driver that it is ready to accept the next character in the buffer. Setting biy 4 of port 2 (0x37a or 0x27a usually) enables interrupt reporting. You can use a simply outb call to set the bit at module initialization.

With interrupts enabled, the parallel interface generates an interrupt whenever the electrical signal at pin 10 changes from LOW to HIGH. 

### Installing an Interrupt Handler

We need some software to actually do something to make interrupts useful. Interrupt lines in hardware are often a limited resource, and the kernel keeps a registry of interrupt lines, similar to the registy of I/O ports. A module is expected to to request an interrupt channel (aka IRQ for interrupt request) before using it, and then release the channel when done with it. Modules are also expected to be able to share interrupt lines with other drivers. 

The following instructions declared in `<linux/interrupt.h>` implement the interrupt registration interface with:

```c
int request_irq(unsigned int irq,
                irqreturn_t (*handler)(int, void *, struct pt_regs *),
                unsigned long flags,
                const char *dev_name,
                void *dev_id);
                
void free_irq(unsigned int irq, void *dev_id);
```

- The value returned from request_irq is 0 on success or a negative error code
- It is common for `-EBUSY` to be returned, meaning another driver is already using the requested interrupt line
- `unsigned int irq` is the interrupt number being processed
- `irqreturn_t (*handler)(int, void *, struct pt_regs *)` is the pointer to the handling function being installed. This is discussed later in the chapter
- `unsigned long flags` is a bit mask of options related to interrupt management
- `const char *dev_name` is the string passed to request_irq used in /proc/interrupts to show the owner of the interrupt
- `void *dev_id` is the pointer used for shared interrupt lines. If not shared, it can be set to NULL, but it is good practice to have this point at something. 
- The bits that can be set in `flags` are the following:
  - `SA_INTERRUPT` indicates a fast interrupt handler. They are executed with interrupts disabled on the current processor
  - `SA_SHIRQ` signals that the interrupt can be shared between devices
  - `SA_SAMPLE_RANDOM` indicates that the generated interrupts can contribute to the entropy pool used by /dev/random and /dev/urandom. These devices return truly random numbers when read and are designed to help application software choose secure keys for encryption. If your interrupts are pretty random, this can help contribute to the system entropy. If your events are not random, do not use this feature. It will make your computer security worse. 

The interrupt handler can be installed either at driver initialization or when the
device is first opened. It is usually better to request interrupts at device open because it allows for more sharing of resources. 

The correct place to call request_irq is when the device is first opened. This happens before hardware is instructed to generate interrupts. The place to call free_irq is the last time the device is closed. This happens after the hardware is told not to interrupt the processor anymore. The disadvantage of this technique is that you have to keep a per-device open count so that you know when to disable interrupts. 

The `short` module requests interrupts at at load time for simplicity, but proper conventions in real drivers should do it at open time. The interrupt requested by the following code is `short_irq`. Code:

```c
if (short_irq >= 0) {
    result = request_irq(short_irq, short_interrupt,
        SA_INTERRUPT, "short", NULL);
    if (result) {
        printk(KERN_INFO "short: can't get assigned irq %i\n",
                short_irq);
        short_irq = -1;
    }
    
    else { /* actually enable it -- assume this *is* a parallel port */
        outb(0x10,short_base+2);
    }
}
```

- This code show a fast handler because of `SA_INTERRUPT`
- It does not support sharing because `SA_SHIRQ` is missing
- It doesn’t contribute to system entropy because `SA_SAMPLE_RANDOM` is missing
- The outb call enables interrupt reporting for the parallel port

Side note: the i386 and x86_64 architectures define a function for querying the availability of an interrupt line with:

```c
int can_request_irq(unsigned int irq, unsigned long flags);
```

- This will return a nonzero value if an attempt at allocating the interrupt succeeds. Things can change between this call and request_irq though so be careful.

### The /proc Interface

Whenever any hardware interrupt reaches a processor, an internal counter is incremented. This provides a way of checking if the device is working properly. Reported interrupts are shown in /proc/interrupts. Snapshot from my computer:

```shell
            CPU0       CPU1       CPU2       CPU3       CPU4       CPU5       CPU6       CPU7       
   0:          7          0          0          0          0          0          0          0  IR-IO-APIC    2-edge      timer
   8:          0          0          0          0          0          0          0          1  IR-IO-APIC    8-edge      rtc0
   9:          0         10          0          0          0          0          0          0  IR-IO-APIC    9-fasteoi   acpi
  14:          0          0          0          0          0          0          0          0  IR-IO-APIC   14-fasteoi   INT34BB:00
 120:          0          0          0          0          0          0          0          0  DMAR-MSI    0-edge      dmar0
 121:          0          0          0          0          0          0          0          0  DMAR-MSI    1-edge      dmar1
 122:          0          0          0          0          0          0          0          0  IR-PCI-MSI 458752-edge      PCIe PME
 123:          0          0          0          2          3          1          0          0  IR-PCI-MSI 466944-edge      PCIe PME, pciehp
 124:          0          0          0          0          0          0          0          0  IR-PCI-MSI 475136-edge      PCIe PME
 125:          0          0          0          0          0          0          0          0  IR-PCI-MSI 487424-edge      PCIe PME, pciehp
 126:          0          0          0      76874          0          0      16764          0  IR-PCI-MSI 327680-edge      xhci_hcd
 127:          0          0         19          0          0          0          0          0  IR-PCI-MSI 57671680-edge      rtsx_pci
 128:          0          0          0          0          0          0          0          0  IR-PCI-MSI 376832-edge      ahci[0000:00:17.0]
 129:          0          0          0          0         71          0          0      45086  IR-PCI-MSI 520192-edge      eno1
 130:          0          0          0          0     459113       1113          0          0  IR-PCI-MSI 32768-edge      i915
 131:          0          1          0          0          0          0         22          0  IR-PCI-MSI 57147392-edge      nvme0q0
 132:       3390          0          0          0          0          0          0          0  IR-PCI-MSI 57147393-edge      nvme0q1
 133:          0      11445          0          0          0          0          0          0  IR-PCI-MSI 57147394-edge      nvme0q2
 134:          0          0       4806          0          0          0          0          0  IR-PCI-MSI 57147395-edge      nvme0q3
 135:          0          0          0       3889          0          0          0          0  IR-PCI-MSI 57147396-edge      nvme0q4
 136:          0          0          0          0       4126          0          0          0  IR-PCI-MSI 57147397-edge      nvme0q5
 137:          0          0          0          0          0       4704          0          0  IR-PCI-MSI 57147398-edge      nvme0q6
 138:          0          0          0          0          0          0       3494          0  IR-PCI-MSI 57147399-edge      nvme0q7
 139:          0          0          0          0          0          0          0       4478  IR-PCI-MSI 57147400-edge      nvme0q8
 140:          0          0          0          0          0          0          0         42  IR-PCI-MSI 360448-edge      mei_me
 141:          0        100          0          4        137          0         36         36  IR-PCI-MSI 333824-edge      iwlwifi: default queue
 142:          0          0          0          0          0          0          0          0  IR-PCI-MSI 333825-edge      iwlwifi: queue 1
 143:          0          0          0          0          0          0          0          0  IR-PCI-MSI 333826-edge      iwlwifi: queue 2
 144:          0          0          0          0          0          0          0          0  IR-PCI-MSI 333827-edge      iwlwifi: queue 3
 145:          0          0          0          0          0          0          0          0  IR-PCI-MSI 333828-edge      iwlwifi: queue 4
 146:          0          0          0          0          0          0          0          0  IR-PCI-MSI 333829-edge      iwlwifi: queue 5
 147:          0          0          0          0          0          0          0          0  IR-PCI-MSI 333830-edge      iwlwifi: queue 6
 148:          0          0          0          0          0          0          0          0  IR-PCI-MSI 333831-edge      iwlwifi: queue 7
 149:          0          0          0          0          0          0          0          0  IR-PCI-MSI 333832-edge      iwlwifi: queue 8
 150:          0          0         33          0          0          0          0          0  IR-PCI-MSI 333833-edge      iwlwifi: exception
 151:          0          0          0       1255          0          0          0          0  IR-PCI-MSI 514048-edge      snd_hda_intel:card0
 NMI:          0          0          0          0          0          0          0          0   Non-maskable interrupts
 LOC:     225584     219038     209798     216187     289561     352760     215586     212748   Local timer interrupts
 SPU:          0          0          0          0          0          0          0          0   Spurious interrupts
 PMI:          0          0          0          0          0          0          0          0   Performance monitoring interrupts
 IWI:          1          2          4          3      29835         62          0          1   IRQ work interrupts
 RTR:          0          0          0          0          0          0          0          0   APIC ICR read retries
 RES:      75053      40532      22896      16088      13327      21284      10879      10596   Rescheduling interrupts
 CAL:      19642      18318      20506      21523      19007      17536      21007      19758   Function call interrupts
 TLB:      27219      25838      29458      29359      26572      26568      27571      28581   TLB shootdowns
 TRM:          0          0          0          0          0          0          0          0   Thermal event interrupts
 THR:          0          0          0          0          0          0          0          0   Threshold APIC interrupts
 DFR:          0          0          0          0          0          0          0          0   Deferred Error APIC interrupts
 MCE:          0          0          0          0          0          0          0          0   Machine check exceptions
 MCP:         11         12         12         12         12         12         12         12   Machine check polls
 HYP:          0          0          0          0          0          0          0          0   Hypervisor callback interrupts
 HRE:          0          0          0          0          0          0          0          0   Hyper-V reenlightenment interrupts
 HVS:          0          0          0          0          0          0          0          0   Hyper-V stimer0 interrupts
 ERR:          0
 MIS:          0
 PIN:          0          0          0          0          0          0          0          0   Posted-interrupt notification event
 NPI:          0          0          0          0          0          0          0          0   Nested posted-interrupt event
 PIW:          0          0          0          0          0          0          0          0   Posted-interrupt wakeup event
```

The first column is the IRQ number. There are missing numbers - this means that the file only shows interrupts corresponding to installed handlers. The last two columns give info on the programmable interrupt controller that handles the interrupt, and the names of the devices that have registered handlers for the interrupt. 

The file /proc/stat gives more info about the low level interrupts, including the number received since boot. Here is a truncated sample output from my system:

```shell
intr 3431494 7 0 0 0 0 0 0 0 1 10
```

The total number of interrupts since boot is that first number 3431494. Future numbers after this one describe the number of interrupts received for each number (from that big list in /proc/interrupts). You can see IRQ line 0 has 7 interrupts, corresponding to that number 7 in the intr line above. 

### Autodetecting the IRQ Number

It is hard to know at initialization time which IRQ line is going to be used by the device. The driver needs this info in order to correctly install the handler. In the short driver, it looks like this:

```c
if (short_irq < 0) /* not yet specified: force the default on */
     switch(short_base) {
         case 0x378: short_irq = 7; break;
         case 0x278: short_irq = 2; break;
         case 0x3bc: short_irq = 5; break;
     }
```

- As a result, short_base defaults to 0x378 and short_irq defaults to 7.

Some devices simple announce which interrupt they are going to use. In this case, the driver retrieves the interrupt number by reading a status byte from one of the device’s I/O ports or PCI configuration space. When the target device can tell the interrupt handler which interrupt to use, autodetecting the IRQ simply means probing the device. Most modern hardware works this way. Other times, we might need to actually probe to autodetect an IRQ number. In this case, the driver tells the device to generate interrupts and watches what happens. If it all goes well, only one interrupt line is activated. 

We will now look at two methods for probing: calling kernel-defined helper functions and
implementing our own version.

#### Kernel-assisted probing

The kernel provides a low-level facility for probing the interrupt number. It only works for non-shared interrupts, but most hardware that uses sharing provides better ways of finding the interrupt number anyways. There are two functions you need which are declared in `<linux/interrupt.h>` and have prototypes:

```c
unsigned long probe_irq_on(void);
    /*This function returns a bit mask of unassigned interrupts. The driver must 
    preserve the returned bit mask, and pass it to probe_irq_off later. After 
    this call, the driver should arrange for its device to generate at least one 
    interrupt. */

int probe_irq_off(unsigned long);
    /*After the device has requested an interrupt, a driver calls this function, 
    passing as its argument the bit mask previously returned by probe_irq_on. 
    probe_irq_off returns the number of the interrupt that was issued after     
    “probe_on.” If no interrupts occurred, 0 is returned. IRQ 0 can’t 
    be probed for, but no custom device can use it on any of the supported 
    architectures anyway. If more than one interrupt occurred (ambiguous 
    detection), probe_irq_off returns a negative value.*/
```

Make sure to only enable interrupts AFTER the call to probe_irq_on and disable them BEFORE calling probe_irq_off. Also remember to service the pending interrupt in your device after probe_irq_off.

This is how the short module probes in this manner:

```c
int count = 0;
do {
     unsigned long mask;
     mask = probe_irq_on( );
     outb_p(0x10,short_base+2); /* enable reporting */
     outb_p(0x00,short_base); /* clear the bit */
     outb_p(0xFF,short_base); /* set the bit: interrupt! */
     outb_p(0x00,short_base+2); /* disable reporting */
     udelay(5); /* give it some time */
     short_irq = probe_irq_off(mask);
     if (short_irq = = 0) { /* none of them? */
     printk(KERN_INFO "short: no irq reported by probe\n");
     short_irq = -1;
     }
      /*
       * if more than one line has been activated, the result is
       * negative. We should service the interrupt (no need for lpt port)
       * and loop over again. Loop at most five times, then give up
       */
} while (short_irq < 0 && count++ < 5);
if (short_irq < 0)
     printk("short: probe failed %i times, giving up\n", count);
```

udelay is used in this example because you may need to wait for a brief period here to give the interrupt time to actually be delivered. Probing could be a lengthy task, maybe requiring 20 ms or longer. It is best to probe for the interrupt only once and at module initialization.

#### DIY probing :question:

This can be implemented without too much trouble (well, the authors claim this, anyways). It is rare that you would need to do this, but it is good to see how it works. The short module performs DIY detecting of the IRQ line if it is loaded with probe=2. 

The mechanism is the same as desribed earlier - we want to enable all unused interrupts and wait and see what happens. Normally, a device be can configured to use one IRQ number from a small set, meaning we only need to look at a few instead of all possible numbers. The short implementation assumes 3,5,7,9 are the only possible IRQ values. 

The following code probes by testing all possible interrupts and looking at what happens. The `trials` array lists the IRQs to try and has 0 as the end marker. The `tried` array is used to keep track of which handlers have actually been registered by the driver. 

Code:

```c
int trials[ ] = {3, 5, 7, 9, 0};
int tried[ ] = {0, 0, 0, 0, 0};
int i, count = 0;

/*
 * install the probing handler for all possible lines. Remember
 * the result (0 for success, or -EBUSY) in order to only free
 * what has been acquired
 */
for (i = 0; trials[i]; i++)
    tried[i] = request_irq(trials[i], short_probing,
        SA_INTERRUPT, "short probe", NULL);
        
do {
     short_irq = 0; /* none got, yet */
     outb_p(0x10,short_base+2); /* enable */
     outb_p(0x00,short_base);
     outb_p(0xFF,short_base); /* toggle the bit */
     outb_p(0x00,short_base+2); /* disable */
     udelay(5); /* give it some time */
     
     /* the value has been set by the handler */
     if (short_irq = = 0) { /* none of them? */
        printk(KERN_INFO "short: no irq reported by probe\n");
     }
     /*
     * If more than one line has been activated, the result is
     * negative. We should service the interrupt (but the lpt port
     * doesn't need it) and loop over again. Do it at most 5 times
     */
} while (short_irq <=0 && count++ < 5);

/* end of loop, uninstall the handler */
for (i = 0; trials[i]; i++)
    if (tried[i] = = 0)
        free_irq(trials[i], NULL);
if (short_irq < 0)
    printk("short: probe failed %i times, giving up\n", count);
```

Sometimes you are not lucky enough to know if there is a small subset of interrupts that the device will be using. In this case, we want to probe from IRQ 0 to IRQ `NR_IRQS`-1, where `NR_IRQS` is defined in <asm/irq.h>. This value highly depends on the platform. 

Now all we are missing is the probing handler itself. The handler's role is to update the `short_irq` according to which interrupts are actually received. A 0 value in `short_irq` means "nothing yet," while a negative value is ambiguous. These values were chosen such that they were consistent with probe_irq_off.

Here is the interrupt handler:

```c
irqreturn_t short_probing(int irq, void *dev_id, struct pt_regs *regs)
{
     if (short_irq = = 0) short_irq = irq; /* found */
     if (short_irq != irq) short_irq = -irq; /* ambiguous */
     return IRQ_HANDLED;
}
```

Arguments to these functions are described later - just know for now that irq is the interrupt being handled is good enough for now. 

### Fast and Slow Handlers

Nowadays there is very little difference between fast and slow interrupts. The main difference is that fast interrupts are executed with all other interrupts disabled on the current processor. Also, you will never see two processors handle  the same IRQ at the same time. 

It is best not to use `SA_INTERRUPT` for your drivers. That is really best used for timer interrupts. So, just use slow interrupts on new systems and you will probably be fine. 

#### The internals of interrupt handling on the x86

The lowest level of interrupt handling can be found in `entry.S`, an assembly-language file that handles most of the machine-level work. A bit of code is assigned to every possible interrupt and in each case, the code pushes the interrupt number on the stack and jumps to a common segment which calls do_IRQ defined in `irq.c`.

do_IRQ first acknowledges the interrupt so the interrupt controller can move on to other things. It then obtains a spinlock ffor the given IRQ number to prevent any other CPU from handling the IRQ. It then clears a few status bits and looks up the handlers for this particular IRQ. If there is no handler, there is nothing to do and the spinlock is released. At the same time, any pending software interrupts are handled and do_IRQ returns. 

Typically, if a device is interrupting, there is at least one handler registered
for its IRQ as well. The function handle_IRQ_event is called to actually invoke the handlers. If the handler is for a slow interrupt (most cases) interrupts are reenabled in the hardware and the handler is invoked. Then, we need to clean stuff up, run the software interrupts, and get back to regular work. 

Probing IRQs is done by setting the IRQ_WAITING status bit for each IRQ that currently lacks a handler. When the interrupt happens, do_IRQ clears that waiting bit and then returns because no handler is registered. When probe_irq_off is called by the driver, it needs to search for only the IRQ that no longer has IRQ_WAITING set. 

### Implementing a Handler

We can write a handler in ordinary c code. Nice!

There are some restrictions on what an interrupt can do - they are actually the same as the restrictions with kernel timers. Some restrictions:

- A handler can't transfer data to or from user space
  - This is because it doesn't execute in the context of a process
- They cannot do anything that would sleep like calling wait_event
- They cannot allocate memory with anything other than `GFP_ATOMIC`
- They cannot lock a semaphore
- They cannot call schedule()

The role of an interrupt handler is to give feedback to its device about interrupt reception and to read or write data according to the meaning of the interrupt being serviced. Sometimes you will have to clear a bit on the interface board that normally means "interrupt pending." Some devices don't require this step, and the parallel port is one of them. As a result, the driver short does not have to clear this bit.

A common task for in interrupt handler is awakening a sleeping process on the device if the interrupt signals the event they are waiting for (like the arrival of new data). 

Make sure to keep interrupt handlers small in execution time. Larger computations should be done with a tasklet or workqueue to schedule the computation at a safer time. 

The sample code in short responds to the interrupt by calling do_gettimeofday and printing the time into a page-sized circular buffer. Next, it awakens any reading process because there is now new data to be read. Code:

```c
irqreturn_t short_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
     struct timeval tv;
     int written;
     
     do_gettimeofday(&tv);
     
     /* Write a 16 byte record. Assume PAGE_SIZE is a multiple of 16 */
     written = sprintf((char *)short_head,"%08u.%06u\n",
            (int)(tv.tv_sec % 100000000), (int)(tv.tv_usec));
     BUG_ON(written != 16);
     short_incr_bp(&short_head, written);
     wake_up_interruptible(&short_queue); /* awake any reading process */
     return IRQ_HANDLED;
}
```

This code is simple which is good. It also calls short_incr_bp which is defined as:

```c
static inline void short_incr_bp(volatile unsigned long *index, int delta)
{
 unsigned long new = *index + delta;
 barrier( ); /* Don't optimize these two together */
 *index = (new >= (short_buffer + PAGE_SIZE)) ? short_buffer : new;
}
```

The following code implements read and write for /dev/shortint:

```c
ssize_t short_i_read (struct file *filp, char __user *buf, size_t count,
                      loff_t *f_pos)
{
     int count0;
     DEFINE_WAIT(wait);
     
     while (short_head = = short_tail) {
        prepare_to_wait(&short_queue, &wait, TASK_INTERRUPTIBLE);
        if (short_head = = short_tail)
            schedule( );
        finish_wait(&short_queue, &wait);
        if (signal_pending (current)) /* a signal arrived */
            return -ERESTARTSYS; /* tell the fs layer to handle it */
     }
     /* count0 is the number of readable data bytes */
     count0 = short_head - short_tail;
     if (count0 < 0) /* wrapped */
        count0 = short_buffer + PAGE_SIZE - short_tail;
     if (count0 < count) count = count0;
     
     if (copy_to_user(buf, (char *)short_tail, count))
        return -EFAULT;
     short_incr_bp (&short_tail, count);
     return count;
}

ssize_t short_i_write (struct file *filp, const char __user *buf, size_t count,
                      loff_t *f_pos)
{
     int written = 0, odd = *f_pos & 1;
     unsigned long port = short_base; /* output to the parallel data latch */
     void *address = (void *) short_base;
     
     if (use_mem) {
        while (written < count)
             iowrite8(0xff * ((++written + odd) & 1), address);
     } else {
        while (written < count)
             outb(0xff * ((++written + odd) & 1), port);
     }
     
    *f_pos += count;
    return written;
}
```

### Handler Arguments and Return Value

Even though short ignores them, there are 3 arguments passed to an interrupt handler:

1. irq
2. dev_id
3. regs

We will now look at the role of each:

- irq is the interrupt number and is useful info you may want to print in your log messages. 
- dev_id is a sort of client data. A void pointer argument is passed to request_irq and this same pointer is then passed back as an argument to the handler when the interrupt happens. You usually pass a pointer to your device data structure in dev_id so that a driver that manages several instances of the same device doesn't need any extra code in the handler to find out which device is in charge of the current interrupt event. 

Here is a typical use of the argument in an interupt handler:

```c
static irqreturn_t sample_interrupt(int irq, void *dev_id, struct pt_regs
 *regs)
{
     struct sample_dev *dev = dev_id;
     
     /* now `dev' points to the right hardware item */
     /* .... */
}
```

An the typical open code then looks like:

```c
static void sample_open(struct inode *inode, struct file *filp)
{
     struct sample_dev *dev = hwinfo + MINOR(inode->i_rdev);
     request_irq(dev->irq, sample_interrupt,
                 0 /* flags */, "sample", dev /* dev_id */);
     /*....*/
     return 0;
}
```

- The last argument `struct pt_regs *regs` is rarely used. It holds a snapshot of the processors context before the processor entered interrupt code. This is not normally needed. 

Interrupt handlers should return a value indicating whether there was actually an interrupt to handle. If the handler found that its device did, indeed, need attention, it should return `IRQ_HANDLED`; otherwise the return value should be `IRQ_NONE`. You can also generate the return value with this macro:

```c
IRQ_RETVAL(handled)
```

- `handled` is nonzero if you were able to handle the interrupt
- The return value is used by the kernel to detect and suppress spurious interrupts
- If your device gives you no way of knowing if it was interrupted, you should return `IRQ_HANDLED` anyways to make it keep running

### Enabling and Disabling Interrupts

Sometimes we need to block interrupts, such as while holding a spinlock. There are ways of disabling interrupts that do not involve spinlocks which will be covered. However, disabling interrupts should be a rare thing to do because it can be dangerous and slow down the system. 

#### Disabling a Single Interrupt

When a driver needs to only disable one specific interrupt line, the kernel provides 3 functions for this purpose. They are declard in `<asm/irq.h>`. You should not really be using these, but here they are:

```c
void disable_irq(int irq);
void disable_irq_nosync(int irq);
void enable_irq(int irq);
```

- All of these update the mask for the specified `irq` in the programmable interrupt controller (PIC). This disables or enables the specified IRQ across all processors. 
- disable_irq disables the given interrupt and waits for a currently executing interrupt handler, if any, to complete
- disable_irq_nosync is the same as disable_irq except it returns immediately

This may be useful for an initial handshake between devices and then disabling that interrupt while packet transmission is going on.

#### Disabling All Interrupts (queue Queen: Don't Stop Me Now, I'm having such a good time)

Alright. Why do you need to do this? Nevermind, here are the functions you are looking for: (they are found in `<asm/system.h>`)

```c
void local_irq_save(unsigned long flags);
void local_irq_disable(void);
```

- local_irq_save disables interrupt delivery on the current processor after saving the current interrupt state into `flags`
- local_irq_disable shuts off local interrupt delivery without saving the state

Turn them all back on with:

```c
void local_irq_restore(unsigned long flags);
void local_irq_enable(void);
```

- local_irq_restore restores that state which was stored into flags by local_irq_save
- local_irq_enable enables interrupts unconditionally

There is no way to disable all interrupts globally across the entire system. You should never need to do this anyways.

### Top and Bottom Halves :first_quarter_moon: :last_quarter_moon:

Okay - now how do we perform lengthy tasks within an interrupt handler? The two needs (work and speed) conflict with each other, leaving the driver writer in a bit of a bind.

Linux fixes all of our problems here by splitting the interrupt handler into two halves:

- The top half is the routine that actually responds to the interrupt. This is the one you register with request_irq
- The bottom half is a routine that is scheduled by the top half to be executed later, at a safer time. 

Typically, the top half:

1. Saves device data to a device-specific buffer
2. Schedules its bottom half
3. Exits

It does all of this very quickly. 

The bottom half then performs whatever work is required such as awakening a process, starting up another I/O operation, and so on. As a result, the top half can service a new interrupt while the bottom half is still working.

Nearly every serious interrupt handler is split in this way. The Linux kernel has two different mechanisms that may be used to implement bottom-half processing, both of which were introduced in chapter 7:

- tasklets are often the preferred mechanism for bottom-half processing
  - They are fast, but all tasklet code must be atomic
- workqueues can also be used
  - They have a higher latency but are allowed to sleep

### Tasklets

Tasklets are a special function that may be scheduled to run, in software interrupt context, at a system-determined safe time. They can be scheduled to run multiple times, but tasklet scheduling is NOT cumulative. The tasklet runs only
once, even if it is requested repeatedly before it is launched. It cannot run in parallel with itself. If your device has multiple tasklets, they must emply some locking to avoid conflicting with one another.

Tasklets are also guaranteed to run on the same CPU as the function that first schedules them. This creates a problem when another interrupt can be delivered while the tasklet is running, so locking between the tasklet and the interrupt handler may still be required.

Tasklets must be declared with a macro:

```c
DECLARE_TASKLET(name, function, data);
```

- `name` is the name to be given to the tasklet
- `function` is the function that is called to execute the tasklet. It takes one unsigned long argument and returns void.
- `data` is an unsigned long value to be passed to the tasklet function

The short driver declares a tasklet as this:

```c
void short_do_tasklet(unsigned long);
DECLARE_TASKLET(short_tasklet, short_do_tasklet, 0);
```

- tasklet_schedule is used to schedule a tasklet for running
- If short is loaded with tasklet=1, it installs a different interrupt handler that saves data and schedules the tasklet as follows:

```c
irqreturn_t short_tl_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
     do_gettimeofday((struct timeval *) tv_head); /* cast to stop 'volatile' warning
    */
     short_incr_tv(&tv_head);
     tasklet_schedule(&short_tasklet);
     short_wq_count++; /* record that an interrupt arrived */
     return IRQ_HANDLED;
}
```

- short_do_tasklet will be executed shortly when convenient. The code for short_do_tasklet is:

```c
void short_do_tasklet (unsigned long unused)
{
     int savecount = short_wq_count, written;
     short_wq_count = 0; /* we have already been removed from the queue */
     /*
     * The bottom half reads the tv array, filled by the top half,
     * and prints it to the circular text buffer, which is then consumed
     * by reading processes
     */
     /* First write the number of interrupts that occurred before this bh */
     written = sprintf((char *)short_head,"bh after %6i\n",savecount);
     short_incr_bp(&short_head, written);
     /*
     * Then, write the time values. Write exactly 16 bytes at a time,
     * so it aligns with PAGE_SIZE
     */
     do {
         written = sprintf((char *)short_head,"%08u.%06u\n",
             (int)(tv_tail->tv_sec % 100000000),
             (int)(tv_tail->tv_usec));
         short_incr_bp(&short_head, written);
         short_incr_tv(&tv_tail);
     } while (tv_tail != tv_head);
     
     wake_up_interruptible(&short_queue); /* awake any reading process */
}
```

A tasklet makes a note of how many interrupts have arrived since it was last called. A driver must take into consideration that many interrupts could pile up while the slower lower half is executing.

### Workqueues

Review: Workqueues invoke a function at some future time in the context of a special worker process. Since the workqueue runs in a process context, it can sleep if need be. You still cannot copy data into user space from a workqueue unless you used some advanced techniques shown in chapter 15. 

The short driver, when loaded with the `wq` option set to a nonzero value, uses a workqueue for its bottom-half processing. It uses the system default workqueue as shown:

```c
static struct work_struct short_wq;
     /* this line is in short_init( ) */
     INIT_WORK(&short_wq, (void (*)(void *)) short_do_tasklet, NULL);
```

The worker function is short_do_tasklet which was shown in a previous section. When working with a workqueue, short introduces another interrupt handler that looks like this:

```c
irqreturn_t short_wq_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
     /* Grab the current time information. */
     do_gettimeofday((struct timeval *) tv_head);
     short_incr_tv(&tv_head);
     
     /* Queue the bh. Don't worry about multiple enqueueing */
     schedule_work(&short_wq);
     
     short_wq_count++; /* record that an interrupt arrived */
     return IRQ_HANDLED;
}
```

The interrupt handler looks a lot like the tasklet version, with
the exception that it calls schedule_work to arrange the bottom-half processing.

### Interrupt Sharing

Modern hardware has been designed to allow the sharing of interrupts. The PCI bus actually requires it! The Linux kernel supports interrupt sharing on all
buses, even those (such as the ISA bus) where sharing has traditionally not been supported. Sharing is caring.

Working with shared interrupts is easy most of the time and should be implemented in your driver. 

### Installing a Shared Handler

Shared interrupts are installed through request_irq just like nonshared ones, but
there are two differences:

1. The `SA_SHIRQ` bit must be specified in the flags argument when requesting the interrupt
2. The dev_id argument must be unique. Any pointer into the module’s address space will do, but dev_id definitely cannot be set to NULL

The kernel maintains a list of shared handlers associated with the interrupt, and dev_id can be thought of as the signature that differentiates between them. When a shared interrupt is requested, request_irq succeeds if one of the following is true:

- The interrupt line is free
- All handlers already registered for that line have also specified that the IRQ is to be shared

A shared handler must be able to recognize its own interrupts and should quickly exit when its own device has not interrupted. You should return IRQ_NONE whenever your handler is called and finds that the device is not interrupting.

No probing function is available for shared handlers. The standard probing mechanism works if the line being used is free, but if the line is already held by another driver with sharing enabled, the probe fails. The good news is most hardware designed for sharing will tell you which interrupt it is using, and you should do this with your drivers too.

Releasing the handler is performed in the normal way with free_irq. Remember to use the correct dev_id when using this function. 

One last note: a driver with a shared handler can’t play with enable_irq or disable_irq. This could really mess up the other devices and their functionality. 

### Running the Handler

When the kernel receives an interrupt, all the registered handlers are invoked. A shared handler must be able to distinguish between interrupts that it needs to handle and ones that are meant for other devices. 

Loading short with the option shared=1 installs the following handler:

```c
irqreturn_t short_sh_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
     int value, written;
     struct timeval tv;
     
     /* If it wasn't short, return immediately */
     value = inb(short_base);
     if (!(value & 0x80))
        return IRQ_NONE;
     
     /* clear the interrupting bit */
     outb(value & 0x7F, short_base);
     
     /* the rest is unchanged */
     do_gettimeofday(&tv);
     written = sprintf((char *)short_head,"%08u.%06u\n",
              (int)(tv.tv_sec % 100000000), (int)(tv.tv_usec));
     short_incr_bp(&short_head, written);
     wake_up_interruptible(&short_queue); /* awake any reading process */
     return IRQ_HANDLED;
}
```

Notes on this code:

- Since the parallel port has no “interrupt-pending” bit to check, the handler uses the ACK bit for this purpose
- The handler resets the bit by zeroing the high bit of the parallel interface’s data port—short assumes that pins 9 and 10 are connected together
- If a different device sharing the IRQ with short generates an interrupt, short sees that its own line is still inactive and does nothing


### The /proc Interface and Shared Interrupts

Using shared handlers in the system doesn’t change /proc/stat, which doesn’t even know about handlers. /proc/interrupts does change a little though. All the handlers installed for the same interrupt number appear on the same line of /proc/interrupts. Example from my system:

```shell
   9:          0         10          0          0          0          0          0          0  IR-IO-APIC    9-fasteoi   acpi
```

We can see from this that IRQ 9 shares between IR-IO-APIC, fasteoi, and acpi. 

### Interrupt-Driven I/O

Whenever a data transfer to or from hardware might be delayed, the driver should implement buffering. Data buffers help to detach data transmission and reception from the write and read system calls. The provides some overall system performance benefits.

A good buffering mechanism leads to interrupt-driven I/O:

- An input buffer is filled at interrupt time
- The buffer is then emptied by processes that read the device
- An output buffer is filled by processes that write to the device
- The output buffer is emptied at interrupt time

An example of this mechanism is shown in /dev/shortprint

For interrupt-driven data transfer to be successful, the hardware needs to be
able to generate interrupts with the following properties:

- For input - the device interrupts the processor when new data has arrived and is ready to be retrieved by the system processor. The actual actions to perform depend on whether the device uses I/O ports, memory mapping, or DMA
- For output - the device delivers an interrupt either when it is ready to accept new data or to acknowledge a successful data transfer. Memory-mapped and DMAcapable devices usually generate interrupts to tell the system they are done with the buffer
