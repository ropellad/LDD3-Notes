# LDD3 Chapter 1 Notes

## Brief Overview:
This book consists of 18 chapters, each of which will be covered in this note set. The book covers up to linux kernel version 2.6.10, which was released 12/25/2004. This is really old, and the most recent version of the linux kernel as of January 2020 is the 5.4.13 kernel. Many important changes have been made over the years, and this note set will attempt to add in as much new detail as possible. 

The first part of the book moves from the software-oriented concepts to the hardware-related ones. This organization is meant to allow you to test the software on your own computer as far as possible without the need to plug external hardware into the machine.

The second half of the book (chapters 12-18) describes block drivers and network interfaces and goes deeper into more advanced topics, such as working with the virtual memory subsystem and with the PCI and USB buses. Many developers do not need all of this material, but it is interesting as a view into how the Linux kernel works.

## Chapter 1. An Introduction to Device Drivers

The linux kernel is a large and complex body of code. People who desire to work with the kernal need an entry point where they can approach the code without being overwhelmed. Device drivers are one good place to start exploring. 

Device drivers are treated like distinct "black boxes" that make a piece of hardware respond to an internal programming interface. User activities are performed with a set of standardized calls that are independent of a specific driver. Mapping these calls to device-specific operations is the job of the driver. Drivers are made such that they can be built separately from the rest of the kernal and "plugged in" at runtime when needed. This modularity is supposed to make the drivers easy to write. 

This book attempts to explain drivers as generically as possible, without going into the specifics for one piece of hardware in too much detail. Many of the generic techniques can be applied to most drivers, though, which makes this book useful for setting up the general framework of any driver.

This chapter does not introduce any code, but reviews important topics for future chapters. 

### The distinction between <i>Mechanism</i> and <i>Policy</i>

One of the best ideas behind Unix design, most programming problems can be split into two parts:

1. The mechanism - What capabilities are to be provided?
2. The policy - How those capabilities can be used?

If these two issues are addressed by different parts of the program, or even different programs, the software package is easier to adapt and develop for particular needs.

A programmer needs to pay attention to this distinction. Write kernel code to access hardware, but don't force policies on the user because different users have different needs. It should offer all the capability of the hardware without adding constraints. The trade-off then becomes a balance between adding as many possible options for the user and possible while keeping it simple so bugs and weird errors don't happen.

Policy-free drivers have the following characteristics:

1. Support for both synchronous and asychronous operation
2. The ability to be opened multiple times
3. The ability to use the full capabilities of the hardware
4. Lack of software layers to "simply things"
5. Tend to work better for end users
6. End up being easier to write and maintain

Being policy-free is often a target for many designers. Many drivers are even released with user programs to help configure and connect to the target device. They can be simple command line utilities or fancy GUIs to help the user. The aim of this book is to cover the kernel, without much detail on the application programs or support library.

In real life, you should always include configuration files that apply a default behavior to underlying mechanisms. 

### Splitting the Kernel

In a Unix-based system, several concurrent processes attend to different tasks. The kernal handles all of the requests that processes make for various system resources. The kernels role can be split into the following parts:

1. Process Management
2. Memory Management
3. Filesystems
4. Device Control
5. Networking

#### 1. Process Management

The kernel is in charge of creating, destroying, and connecting processes. It also schedules the CPU to be shared among many processes. 

#### 2. Memory Management

The kernel builds up the virtual address space for each process. The different parts of the kernel interact with the memory-management subsystem through function calls such as malloc/free among many others. 

#### 3. Filesystems

Almost everything in Unix can be treated as a file. The kernel builds a structured filesystem on top of of unstructured hardware. Linux also supports multiple file system types - all of which must be compatible with the kernel. 

#### 4. Device Control

Device control operations are performed by code that is specific to the device being addressed. That code is called the <i>device driver</i>. Every peripheral on the system must have a driver for it embedded within the kernel. This part of the kernel operation is the focus of this book.

#### Networking

Most network operations are not specific to a process, and incoming packets are asynchronous events. The system is in charge of delivering packets across program and network interfaces, and must control the execution of programs according to their network activity. All routing and addressing issues also go through the kernel. 

### Loadable Modules

A piece of code that can be added to the kernel at runtime is called a module. There are many classes of modules, including device drivers. 

### Classes of Devices 

There are three fundamental device types:

1. char module
2. block module
3. network module

#### 1. Character Devices

A device that can be accessed as a stream of bytes (like a file). A char driver is responsible for implementing this behavior. It usually implements open, close, read, and write system calls. The text console and serial ports are examples of char devices. Char devices are accessed by filesystem nodes, such as <i>/dev/tty1</i> and <i>dev/lp0</i>. The difference between a char device and a regular file is that you can always move back and forth in a regular file, while most char devices are just data channels that can only be accessed sequentially. However, there are still some char devices that look like data areas and can be explored back and forth. 

#### 2. Block Devices

A block device is a device that can host a filesystem. These are also accessed by filesystem nodes in the <i>/dev</i> directory. In Unix, they can usually only handle I/O operations that transfer one or more whole blocks, which may be 512 bytes in length. In Linux, they allow an application to read/write a block like a char device. This means that for Linux, char and block devices only differ in how they are managed internally by the kernel. They have completely different interface to the kernel than char drivers. 

#### 3. Network Interfaces

Any network transaction is made through an interface. Functionally, one device is
able to exchange data with other hosts. Usually, an interface is a hardware
device, but it might also be a pure software device, e.g. the loopback interface. A network driver knows nothing about individual connections; it only handles the sending and receiving of packets. Since this is not stream oriented, a network interface cannot be easily mapped to a node in the filesystem. Instead of read/write functions like char and block drivers, the kernel calls special functions related to packet transmission. 

### Security

Any security check in the system is enforced by kernel code. Security is a policy issue that is often best handled at higher levels within the kernel under the control of the system administrator. There are some exceptions, though:

- Avoid introducing security bugs
- Input from a user process should be treated with great suspicion
- Any memory obtained from the kernel should be zeroed or initialized before the user can access it
- Avoid running unofficial kernels

### Linux Version Numbering

Odd numbered kernels (2.7.x) are not stable. They are short lived development versions.
Even numbered kernels (2.6.x) are stable and should be used for long-term development. 
This is no longer true! Linux 5.5 is stable, just like 5.4 and 5.3. Don't pay attention to the even/odd numbers anymore.

