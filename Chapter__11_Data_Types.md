# LDD3 Chapter 11 Notes

## Chapter 11: Data Types in the Kernel

I feel like this chapter should have come sooner in the book (like chapter 2 or 3) but it is fine to mention it in detail here. 

Chapter Goal: Understand portability issues that arise related to the differences in the same data types between platforms. 

Since Linux is meant to be highly portable, it is important to understand the steps you need to take to ensure that your driver will run the same on several different platforms (if necessary). 

Several issues with kernel developers porting code to x86 came from incorrect data typing. Strict data typing and compiling with the `-Wall` and `-Wstrict-prototypes` flags can prevent most bugs. Data types in the kernel fall into three main types:

1. Standard C types such as `int`
2. Explicitly sized types such as `u32`
3. Types used for specific kernel objects like `pid_t`

If you follow this chapter carefully, you should be able to develop drivers that work on platforms that you might not even be able to test on.

### Use of Standard C Types

Many programmers through around types like int and long willy nilly without regard to any specifics. Writing good drivers requires a little more attention to avoid typing conflicts and really bizarre bugs. Issues arise when you try to use things like a 2 byte filler or something representing a 4 byte string because normal C data types are not the same size on all architechures. See the included file `datasize` to see the size of various data types on your specific platform. Here is the output from my x86_64 machine:

```bash
arch   Size:  char  short  int  long   ptr long-long  u8 u16 u32 u64
x86_64          1     2     4     8     8     8        1   2   4   8
```

And here is the output for an i386 computer:

```bash
arch   Size:  char  short  int  long   ptr long-long  u8 u16 u32 u64
i686            1     2     4     4     4     8       1   2   4   8
```

Already, we can see differences between the long and ptr data types. Here is a more complete table of what happens when you run the program on many different platforms:

```bash
arch Size: char short int long ptr long-long u8 u16 u32 u64
i386        1     2    4    4   4     8       1  2   4   8
alpha       1     2    4    8   8     8       1  2   4   8
armv4l      1     2    4    4   4     8       1  2   4   8
ia64        1     2    4    8   8     8       1  2   4   8
m68k        1     2    4    4   4     8       1  2   4   8
mips        1     2    4    4   4     8       1  2   4   8
ppc         1     2    4    4   4     8       1  2   4   8
sparc       1     2    4    4   4     8       1  2   4   8
sparc64     1     2    4    4   4     8       1  2   4   8
x86_64      1     2    4    8   8     8       1  2   4   8
```

Generic memory addresses in the kernel are usually `unsigned long` to exploit that fact that pointers and long ints are always the same size for all the platforms supported by Linux. Think of memory addresses as just an index into a huge memory array. 

### Assigning an Explicit Size to Data Items

Sometimes you need to know the size of your data to communicate with user space. The kernel offers the following data types whenever you need to know the size of your data. They are found in `<asm/types.h>` and included automatically in `<linux/types.h>`:

```c
u8; /* unsigned byte (8 bits) */
u16; /* unsigned word (16 bits) */
u32; /* unsigned 32-bit value */
u64; /* unsigned 64-bit value */
```

Signed versions of all of these types exist but are rarely used. Simply replace the `u` with an `s` to get the signed version. 

If your are working in user space, you can access these data types with a double underscore like this: `__u8`. These are defined independent of `__KERNEL__`. For example - let's say a driver needs to exchange binary structures with a program running in user space using ioctl. The header files should declare 32 bit fields in the structures as `__u32`. 

These types are Linux specific and may hinder portability to other Unix flavors. Oh well. I hope I don't need to develop for multiple Unix flavors anytime soon. If you do, you can use C99-standard types (there has to be more updated types now?) such as `uint8_t` and `uint32_t`. 

If you see conventional types like `unsigned int` it is likely done for backwards compatibility to the days when Linux had loosy typed structures. 

### Interface-Specific Types

Some common kernel data types have their own typedef statements to prevent portability issues. One example is using `pid_t` instead of `int`. By *interface specific*, we mean a type defined by a library in order to provide an interface to a specific data structure. 

A lot of developers (including me) really hate the use of typedefs and would rather see the real type information used directly in the code. However, a lot of legacy code still uses a lot of typedef stuff. 

Many `_t` types are defined in `<linux/types.h>`. This list is not often useful, however, because when you need a specific data type you will find it in the prototype of the functions you are trying to use. When a driver uses custom types that don't follow the convention, the compiler issues a warning. If you use `-Wall` and are careful to remove warnings, you have a good shot at making portable code. 

One problem comes with printing `_t` data types, since it is not easy to choose the right printk or printf format if these types are architechure dependent. A safe way to do this is to cast the value as the largest possible value to avoid data truncation. 

### Other Portability Issues

Besides data typing, portability issues can arise with explicit constant values. Typically code is parameterized using preprocessor macros. Magic numbers = bad!

#### Time Intervals

When dealing with time intervals, don't assume there are 1000 jiffies per second. When you calculate time intervals using jiffies, scale your times using `HZ`. The number of jiffies corresponding to `msec` milliseconds is `msec*HZ/1000`. 

#### Page Size

A memory page is PAGE_SIZE bytes, NOT 4 KB. Use macros PAGE_SIZE and PAGE_SHIFT instead of hard coding numbers for page sizes. PAGE_SHIFT contains the number of bits to shift an address to get its page number. 

Here is one example of the page size issues being made portable:

If a driver needs 16 KB for temp data, it should not specify and order of 2 to get_free_pages(). You need a more portable solution found by calling get_order:

```c
#include <asm/page.h>
int order = get_order(16*1024);
buf = get_free_pages(GFP_KERNEL, order);
```

- The argument to get_order must be a power of two

### Byte Order

First, here is a good example comparing little and big endian:
[Big Endian and Little Endian Online Reference](https://chortle.ccsu.edu/AssemblyTutorial/Chapter-15/ass15_3.html)

While most PCs store multibyte values with little endian formatting, some high-level platforms use big-endian formatting. Your code should be written so that it doesn't care about byte ordering. The kernel defines a set of macros that handle conversions between the processors native byte ordering and the data you need to store or load in a specific byte order. Here is one set of macros:

```c
//converts a value from whatever the CPU uses to an unsigned, little endian, 32-bit quantity
u32 cpu_to_le32 (u32); 

// converts from the little endian value back to the CPU value
u32 le32_to_cpu (u32);
```

- This works no matter if your CPU uses little/big endian 
- ALso works on non-32-bit platforms.
- You can do this with MANY different kinds of little and big endian data types
  - These are defined in `<linux/byteorder/big_endian.h>` and `<linux/byteorder/little_endian.h>`

### Data Alignment

One final problem to consider is what to do with unaligned data. For example - reading a 4-byte word value stored at an address that isn't a multiple of 4 bytes. To do this, use the following macros:

```c
#include <asm/unaligned.h>
get_unaligned(ptr);
put_unaligned(val, ptr);
```

- These macros are typeless
  - They work for every data item, no matter the byte length
  - They are defined with any kernel version

To write data structures for data items that can be moved across architechures, always enforce natural alignment of the data items along with standardizing on a specific endianness. 

- Natural alignment means storing data items at an address that is a multiple of their size (for example, 8-byte items go in an address multiple of 8).
- Then use filler fields to avoid leaving holes in that data structure. 

See the sample program dataalign in the misc-progs directory of the sample code to show how alignment is enforced by the compiler. 

The output from my system is:

```bash
arch  Align:  char  short  int  long   ptr long-long  u8 u16 u32 u64
x86_64          1     2     4     8     8     8        1   2   4   8
```

And the output from the kdataalign kernel module for my system is shown below:

```bash
[Mar17 19:42] kdataalign: unknown parameter 'kdataalign' ignored
[  +0.000048] arch  Align:  char  short  int  long   ptr long-long  u8 u16 u32 u64
[  +0.000002] x86_64          1     2     4     8     8     8        1   2   4   8
```

A compiler may also automatically pad without telling you, and this could cause issues. So be aware of this. The workaround for this issue is to tell the compiler that the structure must be packed with no fillers added. One example is found in the kernel header file `<linux/edd.h>` which defines data structures used in interfacing with the x86 BIOS:

```c
struct {
       u16 id;
       u64 lun;
       u16 reserved1;
       u32 reserved2;
} __attribute__ ((packed)) scsi;
```

Without the `__attribute__ ((packed))`, the lun field would be preceded by two filler bytes or six if we compile the structure on a 64-bit platform. 

# %% I want to go over packing in a little bit more detail and how this actually happens and why we need it.

### Pointers and Error Values (I can point to errors all day long)

Several internal kernel functions return a pointer value to the caller. These functions can also fail (welcome to the club). In most cases, failure is indicated by returning a NULL pointer value. While this works, it does very little to describe the actual nature of the problem. There are times when the caller needs an actual error code so that the right decision can be made as to what to do next in a program. 

A function returning a pointer type can return an error value with:

```c
void *ERR_PTR(long error);
```

- Error is the usual negative error code

The caller can use `IS_ERR` to test whether a returned pointer is an error code or not with:

```c
long IS_ERR(const void *ptr);
```

THe actual error code can be extracted with:

```c
long PTR_ERR(const void *ptr);
```

- Only use `PTR_ERR` on a value for which `IS_ERR` returns a true value.


### Linked Lists

OS kernels often need to maintain lists of data structures. Developers have created a standard implementation of circular, doubly linked lists. People who need to manipulate lists are encouraged to use this facility.

Linked lists in the kernel do not perform locking. You need to implement your own locking to use these responsibly. To use the list mechanism, include the file `<linux/list.h>` and look for the structure of type `list_head`:

```c
struct list_head {
    struct list_head *next, *prev;
};
```

- Linked lists used in real code are usually made up of some type of structure
  - Each one describes one entry in the list
- You need only embed a `list_head` inside the structures that make up the list

An example in your code may look like this:

```c
struct todo_struct {
     struct list_head list;
     int priority; /* driver specific */
     /* ... add other driver-specific fields */
};
```

List heads must be initialized prior to use with the INIT_LIST_HEAD macro. A “to do” list head could be declared and initialized with:

```c
struct list_head todo_list;
INIT_LIST_HEAD(&todo_list);
```

Alternatively, lists can be initialized at compile time:

```c
LIST_HEAD(todo_list);
```

Review of doubly linked lists:
[Doubly Linked List \| Set 1 (Introduction and Insertion) - GeeksforGeeks](https://www.geeksforgeeks.org/doubly-linked-list/)

# %% Go over this list head thing and why it needs to be initialized. 

Several functions are defined in `<linux/list.h>` that work with lists:

```
list_add(struct list_head *new, struct list_head *head);
    Adds the new entry immediately after the list head—normally at the beginning of
    the list. Therefore, it can be used to build stacks. Note, however, that the head
    need not be the nominal head of the list. If you pass a list_head structure that
    happens to be in the middle of the list somewhere, the new entry goes immediately   
    after it. Since Linux lists are circular, the head of the list is not generally 
    different from any other entry.
    
list_add_tail(struct list_head *new, struct list_head *head);
    Adds a new entry just before the given list head—at the end of the list, in other
    words. list_add_tail can, thus, be used to build first-in first-out queues.
    
list_del(struct list_head *entry);
list_del_init(struct list_head *entry);
    The given entry is removed from the list. If the entry might ever be reinserted
    into another list, you should use list_del_init, which reinitializes the linked list
    pointers.
    
list_move(struct list_head *entry, struct list_head *head);
list_move_tail(struct list_head *entry, struct list_head *head);
    The given entry is removed from its current list and added to the beginning of
    head. To put the entry at the end of the new list, use list_move_tail instead.
    
list_empty(struct list_head *head);
    Returns a nonzero value if the given list is empty.
    
list_splice(struct list_head *list, struct list_head *head);
    Joins two lists by inserting list immediately after head.
    
```

The `list_head` structures are good for implementing a list of like structures. However, the invoking program is usually more interested in the larger structures that make up the list as a whole. 

# %% Why tho? ^^

A macro called list_entry is provided that maps a list_head structure pointer back into a pointer to the structure that contains it. It is used like this:

```c
list_entry(struct list_head *ptr, type_of_struct, field_name);
```

- `ptr` is a pointer to the `struct list_head` being used
- `type_of_struct` is the type of the structure containing the `ptr`
- `field_name` is the name of the list field within the structure

To turn a list entry into its containing structure, you could do this (where tehe list field is just called `list`):

```c
struct todo_struct *todo_ptr =
 list_entry(listptr, struct todo_struct, list);
```

The author claims that the traversal of linked lists is easy. We will see. Here is an example given for keeping the list of todo_struct items sorted in descending priority order. A function to add a new entry would look like this:

```c
void todo_add_entry(struct todo_struct *new)
{
     struct list_head *ptr;
     struct todo_struct *entry;
     for (ptr = todo_list.next; ptr != &todo_list; ptr = ptr->next) {
         entry = list_entry(ptr, struct todo_struct, list);
         if (entry->priority < new->priority) {
             list_add_tail(&new->list, ptr);
             return;
         }
     }
     list_add_tail(&new->list, &todo_struct)
}
```

However, instead of doing this there is a better method using predefined macros for creating and iterating through linked lists. The previous example could be rewritten as:

```c
void todo_add_entry(struct todo_struct *new)
{
     struct list_head *ptr;
     struct todo_struct *entry;
     
     list_for_each(ptr, &todo_list) {
         entry = list_entry(ptr, struct todo_struct, list);
         if (entry->priority < new->priority) {
             list_add_tail(&new->list, ptr);
             return;
         }
     }
     list_add_tail(&new->list, &todo_struct)
}
```

Use the macros as much as possible. It will probably work better than your code and you will make less mistakes. A few variants of these macros exist:

```
list_for_each(struct list_head *cursor, struct list_head *list)
    Creates a for loop that executes once with cursor pointing at each
    successive entry in the list. Be careful about changing the list while iterating
    through it.
    
list_for_each_prev(struct list_head *cursor, struct list_head *list)
    This one iterates backwards through the list
    
list_for_each_safe(struct list_head *cursor, struct list_head *next, struct list_head *list)
    If your loop may delete entries in the list, use this version. It simply stores the
    next entry in the list in next at the beginning of the loop, so it does not get 
    confused if the entry pointed to by cursor is deleted.
    
list_for_each_entry(type *cursor, struct list_head *list, member)
list_for_each_entry_safe(type *cursor, type *next, struct list_head *list, member)
    These ease the process of dealing with a list containing a given type of
    structure. Cursor is a pointer to the containing structure type. Member
    is the name of the list_head structure within the containing structure. With
    these macros, there is no need to put list_entry calls inside the loop.
```

Inside `<linux/list.h>` are additional declarations. hlist is a doubly linked list with a separate, single-pointer list head type used for hash tables. There is some other stuff related to a Read-Copy-Update mechanism. You have options if you need them. 