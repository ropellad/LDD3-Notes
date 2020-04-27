# LDD3 Chapter 5 Notes

## Chapter 5: Concurrency and Race Conditions

The purpose of this chapter is to introduce the problem of concurrency and how the kernel provides tools to deal with it. Concurrency is trying to more than one thing at once. How do you run hundreds of processes with only a few cores of a CPU? From the user side, it looks simple to run a web browser at the same time as a word processor, music player, IDE, etc. But underneath, a lot of magic happens to make the simultaneous processes work without anyone getting confused or forgotten. 

Device drivers must incorporate concurrency from the very beginning of development. We first look at what can go wrong with concurrency, then look at the tools that exist within the kernel to eliminate those problems. 

### Pitfalls in Scull :skull:

Deep within the write logic of scull, the following code is implemented:

```c
 if (!dptr->data[s_pos]) {
    dptr->data[s_pos] = kmalloc(quantum, GFP_KERNEL);
    if (!dptr->data[s_pos])
        goto out;
 }
```

This code decides if the memory it requires has been allocated yet. Let's say two independent devices are trying to write to the same scull device, called device A and device B. Let's also say that they are trying to write to the same offset. Each process reaches the first if condition in the first line at the same exact time. If the pointer is NULL, each process will decide to allocate memory, and each will assign the resulting pointer to `dptr->data[s_pos]`. 

What happens? The second process will win! The assignment by A will be overwritten by the "more-behind" process B. Early bird gets the worm, but the second mouse gets the cheese. This level of concurrency is like two mice with a mouse trap. 

So what happens to device A? Well, scull will forget entirely about the memory allocated to A, and the memory will be dropped and never returned to the system. This is bad!

This sequence of events is a demonstration of a race condition. Race conditions happen with uncontrolled access to shared data. When a wrong process happens, unexpected behavior happens.  For the race condition discussed above, the result is a memory leak (very bad!). Race conditions can often lead to system crashes, corrupted data, or security problems as well. You may be tempted to disregard race conditions as extremely low probability events. But with computers, one-in-a-million events can happen every few seconds making these problems anything but rare. 

### Concurrency and Its Management

Modern Linux has MANY sources of concurrency possible race conditions. Various parts of the system can interact with your code in a large number of ways. Here are some of them:

- SMP systems can be executing the same code simultaneously on multiple processors
- Your driver's code can lose a processor at any time, and the next process that replaces it could also be running in your driver
- Device interrupts can screw up everything
- There is also delayed code execution like workqueues, tasklets, and timers
- Your device could simply be unplugged while working with it (safety eject is *so* overrated!)

Race conditions can be intimidating, but the kernel provides ways to manage the battlefield of processes. Most race conditions can be avoided through control primitives in combination with the application of a few basic principles. Here are those principles:

Race conditions are caused by shared access to resources. When two threads of execution access the same data structures or hardware resources, the potential for errors and mixups always exists. <b>So avoid shared resources whenever possible.</b> If there is no concurrent access, there can be no race conditions. Kernel code should minimize sharing. One way to implement this idea is to avoid the use of global variables. Minimizing globals is not just for kernel code, either. 

Unfortunately, sometimes you *need* to share something with a driver. We need to set things up so that the code can sees memory that has been allocated or know that no memory has been or will be allocated by anybody else. The technique for access management is called *locking* or *mutual exclusion*. It makes sure that only one thread can manipulate a shared resource at a time. 

One other principle: when kernel code creates an object shared with any other part of the kernel, that object must continue to exist until it is known that no outside references exist to it. It my thoughts: don't turn off the lights unless you know you are the last one to leave the room. Two big requirements come out of this rule:

1. No object can be made available to the kernel until it is in a state where it can function properly
2. References to such objects must be tracked. Most of the time, the kernel handles reference counting

### Semaphores and Mutexes

How do we add locking to scull? First, we need to create *critical sections:* code that can be executed by only one thread at a time. 

There are different kinds of critical sections, and the kernel provides different primitives for different needs. Every access to scull happens as a result of direct user request. No access is allowed from interrupt handlers or other asynchronous things. There are also no latency requirements. Scull also does not hold any other critical system resource while it is accessing its own data. Why does all of this matter? If the scull driver goes to sleep while waiting for its turn to access the data structure, nobody is going to mind. 

What does it mean to "go to sleep?" When a process reaches a state where it can no longer make any further processes, it goes to sleep (aka blocks). This allows someone else to use the processor until a future time when the process has work to do. Processes often sleep when waiting for user I/O to complete. Sometimes in the kernel you cannot sleep. The kernel must be like having kids. 

For our critical sections to work properly, we must use a locking primitive that that works when a thread that owns the lock goes to sleep. Not all locking mechanisms can be used when sleeping is a possibility. However, for scull, the mechanism that works best is a *semaphore*.

Semaphores are well-understood in computer science, just not in my head (yet). Basically, a semaphore is a single integer value combined with a pair of functions that are typically called P and V. A process wishing to enter a critical section of data will first call P on the relevant semaphore. If the semaphore’s value is greater than zero, that value is decremented by one and the process continues. If, instead, the semaphore’s value is 0 or less, the process must wait until somebody else releases the semaphore. Unlocking a semaphore is accomplished by calling V. The function V increments the value of the semaphore and wakes up waiting processes if needed. When semaphores are used for mutual exclusion their value will initially be set to 1. This kind of semaphore can only be held by a single process or thread at a given time. Used this way, the semaphore is sometimes called a *mutex* which is the abbreviation for mutual exclusion. Almost all semaphores in the linux kernel are used in this way. 

### The Linux Semaphore Implementation

To use semaphores, you must include `<asm/semaphore.h>`. The relevant type is `struct semaphore`. They can be declared and initialized in several ways. One way is to create a semaphore directly, then set it up with `sema_init`:

```c
void sema_init(struct semaphore *sem, int val);
```

`val` is the initial value of the semaphore (use 1 for a mutex)

The kernel provides some special macros to declare and initialize a mutex specifically with:

```c
DECLARE_MUTEX(name);
DECLARE_MUTEX_LOCKED(name);
```

The result of this is a semaphore variable called `name` that is initialized to 1 with `DECLARE_MUTEX(name);` and 0 with `DECLARE_MUTEX_LOCKED(name);`. Why even use the second case? Well - the mutex will start in a locked state and will need to be explicitly unlocked before any thread is allowed access (start with the gate closed, and you decide when to open it). 

If the mutex must be initialized dynamically at runtime, use one of the following:

```c
void init_MUTEX(struct semaphore *sem);
void init_MUTEX_LOCKED(struct semaphore *sem);
```

In LinuxLand (the greatest thing since CandyLand), the P function is called *down* because is decrements the integer value of the semaphore. There are 3 versions of down:

```c 
void down(struct semaphore *sem);
```
This one decrements the value of the semaphore and waits as long as need be.


```c
int down_interruptible(struct semaphore *sem);
```
This one is the same as above, but the process is interruptable. This is the one you should use, because you don't want a non-interruptable process. But this also requires some extra care, because you need to check the return value and understand that the caller does NOT hold the the semaphore at that point. 


```c
int down_trylock(struct semaphore *sem);
```
This version never sleeps (see *kids* from earlier in these notes). If the semaphore is not available at the time of the call, down_trylock returns immediately with a nonzero return value.

When a thread successfully calls one of the versions of *down*, it is said to be holding the semaphore (aka taken out, aka aquired). That thread can now access the critical section protected by the semaphore. When the operation is complete, the semaphore must be returned and the function V is called. In LinuxLand, V is known as *up*. Code:

```c
void up(struct semaphore *sem);
```

Once up has been called, the caller no longer holds the semaphore and someone else can take over. Make sure to release a semaphore before returning errors that might happen while a semaphore is being held. 

### Using Semaphores in scull :skull:

To properly use locking primitives, specify exactly which resources are to be protected and make sure that every access to those resources uses the proper locking. In scull, everything of interest is contained within the `skull_dev` structure, so that is a logical scope for locking.

In scull, the code looks like this:

```c
struct scull_dev {
   struct scull_qset *data; /* Pointer to first quantum set */
   int quantum; /* the current quantum size */
   int qset; /* the current array size */
   unsigned long size; /* amount of data stored here */
   unsigned int access_key; /* used by sculluid and scullpriv */
   struct semaphore sem; /* mutual exclusion semaphore */
   struct cdev cdev; /* Char device structure */
};
```

The structure called `sem` is the semaphore. A separate semaphore is used for each virtual skull device since multiple devices can be written to simultaneously with no race conditions involved. Semaphores must be initialized before use. Scull performs this initialization at load time with this:

```c
for (i = 0; i < scull_nr_devs; i++) {
    scull_devices[i].quantum = scull_quantum;
    scull_devices[i].qset = scull_qset;
    init_MUTEX(&scull_devices[i].sem);
    scull_setup_cdev(&scull_devices[i], i);
}
```

The semaphore must be initialized before the scull device is made available to the rest of the system to avoid race conditions. Thus, `init_MUTEX` is called before `scull_setup_cdev`. 

The next step is to go through the code and and ensure that no access to the `scull_dev` data structure is made without holding the semaphore. For example, scull_write begins with this:

```c
if (down_interruptible(&dev->sem))
    return -ERESTARTSYS;
```

When the check on the return value of down_interruptible - if it returns nonzero, the operation was interrupted (meaning we probably shouldn't continue!). The usual thing to do in this situation is to return `-ERESTARTSYS`. When seeing this return code, the higher layers of the kernel will either restart the call from the beginning or return the error to the user. If you return `-ERESTARTSYS`, you must first undo any user-visible changes that might have been made so that proper behavior happens when the system call is tried again. If you cannot undo things this way, you should return `-EINTR` instead.

After doing everything you need to do while holding the semaphore (whether you are successful or not), scull_write must release it. Here are a few lines that do this:

```c
out:
 up(&dev->sem);
 return retval;
```
The code frees the semaphore and returns the status called for. There are many places where errors could happen when writing, so each failure can have a `goto out` statement to free the semaphore and return a value. 

### Reader/Writer Semaphores

Semaphores perform mutual exclusion for all callers, but sometimes we want multiple processes to have read access to critical data at the same time, as long as nobody writes to it. This read-only action doesn't need a mutex, and we can speed up our program as a result. The Linux kernel provides a special type of semaphore called a *rwsem*, short for reader/writer semaphore for this situation. The use of rwsems in drivers is pretty rare, but are occasionally useful. Code using rwsems must include `<linux/rwsem.h>`. The relevant data type is `struct rw_semaphore`, and must be explicitly intialized at runtime with:

```c
void init_rwsem(struct rw_semaphore *sem);
```

The interface for code needing read-only access is:

```c
void down_read(struct rw_semaphore *sem);
int down_read_trylock(struct rw_semaphore *sem);
void up_read(struct rw_semaphore *sem);
```

The regular `down_read` could put you into an uninterruptable sleep, while the `down_read_trylock` version is interruptable. The rwsem obtained with `down_read` must be freed with `up_read`. 

The interface for writers is similar:

```c
void down_write(struct rw_semaphore *sem);
int down_write_trylock(struct rw_semaphore *sem);
void up_write(struct rw_semaphore *sem);
void downgrade_write(struct rw_semaphore *sem);
```

Again, write has an interruptable version called with the `_trylock`. The `downgrade_write` version is used if your writer needs to quickly change something, then needs read-only access for the rest of the time and can free the writing to another process. 

Read/write semaphores allow one writer or an unlimited number of readers to hold the semaphore. Writers get priority, and as soon as a writer enters the critical section, no readers are allowed until the writer has completed all work. This can lead to reader starvation, where readers must wait a very long time to read. Thus, rwsems are best for minimal writes and lots of reading. 

### Completions

It might be tempting to use a semaphore for synchronization of tasks. For example:

```c
struct semaphore sem;

init_MUTEX_LOCKED(&sem);
start_external_task(&sem);
down(&sem);
```
The external task can then call up(&sem) when its work is done.

Semaphores are actually not the best tool to use in this situation. It will make performance suffer a LOT. Semaphores are not locked very often, and have been optimized for that case, not for when speed is critical. As such, the Linux 2.4.7 kernel introduced a completion interface as a lightweight mechanism with only one task: allowing one thread to tell another that the job is done. To use completions, include `<linux/completion.h>`. It can be created with:

```c
DECLARE_COMPLETION(my_completion);
```

Or dynamically with:

```c
struct completion my_completion;
/* ... */
init_completion(&my_completion);
```

Waiting for completion is only one line of code:

```c
void wait_for_completion(struct completion *c);
```

Beware: this could create an uninterruptable process if completion never happens with the child process. 

To signal completion, use:

```c
void complete(struct completion *c);
void complete_all(struct completion *c);
```

The first one will wake up only one of the waiting threads. The second will wake up all waiting threads. Most of the time, there is only one process waiting, so the functions produce identical outcomes. There is another difference between them, however:

A completion is normally a one-shot device. It is used once then discarded. It is possible to reuse completion structures though. If complete_all is not used, a completion structure can be reused without any problems as long as there is no ambiguity about what event is being signaled. If you use complete_all, however, you must reinitialize the completion structure before reusing it.

Do quick re-initialization with this:

```c
INIT_COMPLETION(struct completion c);
```

The *complete* module included in the example source defines a device with simple semantics. Any device that attempts to read from the device will wait until some other process writes to the device. Code:

```c
DECLARE_COMPLETION(comp);
ssize_t complete_read (struct file *filp, char __user *buf, size_t count, loff_t
*pos)
{
   printk(KERN_DEBUG "process %i (%s) going to sleep\n",
   current->pid, current->comm);
   wait_for_completion(&comp);
   printk(KERN_DEBUG "awoken %i (%s)\n", current->pid, current->comm);
   return 0; /* EOF */
}
ssize_t complete_write (struct file *filp, const char __user *buf, size_t count,
        loff_t *pos)
{
   printk(KERN_DEBUG "process %i (%s) awakening the readers...\n",
   current->pid, current->comm);
   complete(&comp);
   return count; /* succeed, to avoid retrial */
}
```

It is possible to have multiple processes reading from the device at the same time (this is fine!). Each write to the device will cause one read operation to complete, but we have no idea which read process will be the lucky winner. What is the use for a mechanism like this? One case is with kernel thread termination at module exit time. In most cases, some driver internal workings are performed by a kernel thread in a while(1) loop (Typically I hate seeing while(1) loops, but we do use them a lot in embedded systems). When the module is ready for cleanup, the exit function tells the thread to exit, then waits for completion. The kernel actually includes a specific function to be used by the thread:

```c
void complete_and_exit(struct completion *c, long retval);
```

# Again, `retval` could be anything. The pointer `c` is what?

### Spinlocks (You Spin Me Right Round)

Semaphores are useful for mutual exclusion (mutexs), but are not the only tool provided by the kernel to do this. Most locking is actually implemented with a spinlock. Spinlocks are different than semaphores in that they can be used in code that cannot sleep like interrupt handlers. Spinlocks offer better performance in general, but they bring a different set of constraints. 

The concept of a spinlock is simple: it is a mutual exclusion device that can only have two values - "locked" and "unlocked." This is usually done with a single bit integer value. Code that wants to take out a lock tests the relevant bit. If the lock is available, the bit is set to locked and the code continues to the critical section. If the lock has been taken by somebody else, the code goes into a tiny loop and repeatedly checks the lock until it finally is available. This is the spin part of the spinlock. 

Real implementation is a bit more complex, and the "test and set" operation has to be done in an atomic manner so that only one thread can obtain the lock, even if several are spinning at the same time. You must also be careful to avoid deadlocks on hyperthreaded processors where 1 core can share cache with 2 virtual processors. 

# How does the spinlock mechanism avoid race conditions itself?

### Intro to Spinlock API

Include file for spinlock primitives: `<linux/spinlock.h>`. The lock has type spinlock_t and must be initialized. To initialize at compile time use:

```c
spinlock_t my_lock = SPIN_LOCK_UNLOCKED;
```
 and at runtime use:
 
```c
void spin_lock_init(spinlock_t *lock);
```

To obtain the lock, your code must have the following:

```c
void spin_lock(spinlock_t *lock);
```

Careful: All spinlock waits are uninterruptable. Once called, spin_lock will keep spinning until the lock is available. 

To release the lock, your code should look something like this:

```c
void spin_unlock(spinlock_t *lock);
```

There are much more spinlock functions we will look at shortly, but for now these are the core ideas that all of them follow. 

### Spinlocks and Atomic Context

A good rule to avoid system deadlocks:

Any code must, while holding a spinlock, be atomic. It cannot sleep and it cannot relinquish the processor for any reason except *sometimes* to service interrupts. 

Kernel preemption is handled by the spinlock code itself - meaning that when code holds a spinlock, preemption is disabled on the relevant processor. 

Avoiding sleep while holding a lock can be more difficult (maybe the kernel should just have a few kids?). Many kernel functions can sleep and this behavior is not well documented. Even things like kmalloc() can sleep until memory is free. Thus, you must be careful with every function that is used in the spinlock. 

Last rule: spinlocks should be held for the minimum amount of time possible. It can increase kernel latency and make other programs run slow. (Paging Google Stadia developers here :laughing: !)

### The Spinlock Functions

We have seen spin_lock and spin_unlock to manipulate spinlocks. There are four functions that can lock a spinlock:

```c
void spin_lock(spinlock_t *lock);
void spin_lock_irqsave(spinlock_t *lock, unsigned long flags);
void spin_lock_irq(spinlock_t *lock);
void spin_lock_bh(spinlock_t *lock)
```
- `spin_lock` was already covered before
- `spin_lock_irqsave` disables interrupts on the local processor before taking the spinlock. The previous interrupt state is stored in `flags`
- `spin_lock_irq` does not save the previous state. Only use if you are certain nothing else before you disabled interrupts. 
- `spin_lock_bh` disables software interrupts before taking the lock but leaves hardware interrupts enabled

If the spinlock can be taken by code that runs in interrupt context, you must use one of the forms that disables interrupts to avoid deadlock. If it is only implemented inside of a software interrupt, use the `spin_lock_bh` to maintain the hardware interrupt features while being deadlock safe. 

There are also four ways to release a spinlock which correspond to each of the locking functions:

```c
void spin_unlock(spinlock_t *lock);
void spin_unlock_irqrestore(spinlock_t *lock, unsigned long flags);
void spin_unlock_irq(spinlock_t *lock);
void spin_unlock_bh(spinlock_t *lock);
```

The flags argument passed to `spin_unlock_irqrestore` must be the same variable passed to `spin_lock_irqsave`. You must also call `spin_lock_irqsave` and `spin_ unlock_irqrestore` in the same function to avoid breaking on some architectures. 

There is also a set of nonblocking spinlock operations:

```c
int spin_trylock(spinlock_t *lock);
int spin_trylock_bh(spinlock_t *lock);
```

These functions return nonzero on success, meaning the lock was obtained. They return 0 otherwise. 

# Go over this ^^^ and how it is used versus the other spinlocks

### Reader/Writer Spinlocks

The kernel provides a reader/writer set of spinlocks directly analogous to the reader/writer semaphores we saw earlier in the chapter. Again, lots of readers can all have access at once, but one writer must have exclusive access. R/W locks have a type `rwlock_t` defined in `<linux/spinlock.h>`. There are two ways to declare and initialize (statically and dynamically). 

```c
rwlock_t my_rwlock = RW_LOCK_UNLOCKED; /* Static way */

rwlock_t my_rwlock;
rwlock_init(&my_rwlock); /* Dynamic way */
```

The same variants of the four lock and unlock functions are available. Note there is no trylock for the reader. Not sure why...

```c
void read_lock(rwlock_t *lock);
void read_lock_irqsave(rwlock_t *lock, unsigned long flags);
void read_lock_irq(rwlock_t *lock);
void read_lock_bh(rwlock_t *lock);

void read_unlock(rwlock_t *lock);
void read_unlock_irqrestore(rwlock_t *lock, unsigned long flags);
void read_unlock_irq(rwlock_t *lock);
void read_unlock_bh(rwlock_t *lock);
```

Write access has the familiar set of functions as well:

```c
void write_lock(rwlock_t *lock);
void write_lock_irqsave(rwlock_t *lock, unsigned long flags);
void write_lock_irq(rwlock_t *lock);
void write_lock_bh(rwlock_t *lock);
int write_trylock(rwlock_t *lock);

void write_unlock(rwlock_t *lock);
void write_unlock_irqrestore(rwlock_t *lock, unsigned long flags);
void write_unlock_irq(rwlock_t *lock);
void write_unlock_bh(rwlock_t *lock);
```

Same as semaphores - don't starve the readers and don't have lengthy chuncks to write frequently. It will really slow down the system!

### Locking Traps:

Many things can go wrong with trying to implement locks correctly. Here are some common modes of failure:

1. Ambiguous Rules
   - You can't aquire the same lock twice. Therefore, you have to write some functions with the assumption that their caller has already acquired the relevant lock(s).  Usually, only your internal, static functions can be written in this way. If you do this, DOCUMENT IT! It's crap to look through someone else's code and try to figure out their assumptions with locking.
    
2. Lock Ordering Rules
    - If two processes need two locks each, and they both currently have one, you will hit a deadlock. When multiple locks must be acquired, they should always be acquired in the same order. Also, aquire semaphores first because of the sleeping that can go on with them. Overall, try to avoid needing multiple locks altogether. 

3. Fine- Versus Coarse-Grained Locking
    - The kernel has thousands of locks which is nice for systems with many processors. But in a kernel with thousands of locks, it can be hard to know which locks you need and which order to aquire them in. When implementing locks with a device driver, start big with one overall lock and then fine tune it from there. Don't optimize from the beginning. 

### Alternatives to Locking

There are situations to avoid locks where atomic access can do the trick instead. Here are some ways of avoiding locks:

1. Lock-Free Algorithms
    - Sometimes you can recast your algorithms to avoid the need for locking altogether. Many reader/writer situations can work in this manner if there is only one writer. If the writer takes care that the view of the data structure, as seen by the reader, is always consistent, it may be possible to create a lock-free data structure. One such data structure is the circular buffer. The producer places data into one part of the array, while the consumer removes data from the other. When the end of the array is reached, the producer wraps back around to the beginning (like moving in a circle with the data writes turning into a game of cat and mouse). A circular buffer requires an array and two index values to trackwhere the next new value goes and which value should be removed from the buffer next. These things are actually really cool. There is one producer and one consumer, and they will never have race conditions. See `<linux/kfifo.h>` for a circular buffer implementation in the kernel. 

2. Atomic Variables
    - Sometimes a shared resource is a simple integer value. Suppose your driver has a shared variable `n_op` that tells how many device operations are currently outstanding. Something like `n_op++` would normally need locking, but luckily the kernel provides an atomic integer type called `atomic_t` found in `<asm/atomic.h>`. Here are some atomic functions:

```c
void atomic_set(atomic_t *v, int i);
atomic_t v = ATOMIC_INIT(0);
    //Set the atomic variable v to the integer value i. You can also initialize atomic values at compile time with the ATOMIC_INIT macro.
    
int atomic_read(atomic_t *v);
    //Return the current value of v.
    
void atomic_add(int i, atomic_t *v);
    //Add i to the atomic variable pointed to by v. The return value is void, because there is an extra cost to returning the new value, and most of the time there’s no need to know it.
    
void atomic_sub(int i, atomic_t *v);
    //Subtract i from *v.
    
void atomic_inc(atomic_t *v);
void atomic_dec(atomic_t *v);
    //Increment or decrement an atomic variable.
    
int atomic_inc_and_test(atomic_t *v);
int atomic_dec_and_test(atomic_t *v);
int atomic_sub_and_test(int i, atomic_t *v);
    //Perform the specified operation and test the result; if, after the operation, the atomic value is 0, then the return value is true; otherwise, it is false. Note that there is no atomic_add_and_test.
    
int atomic_add_negative(int i, atomic_t *v);
    //Add the integer variable i to v. The return value is true if the result is negative, false otherwise.
    
int atomic_add_return(int i, atomic_t *v);
int atomic_sub_return(int i, atomic_t *v);
int atomic_inc_return(atomic_t *v);
int atomic_dec_return(atomic_t *v);
    //Behave just like atomic_add and friends, with the exception that they return the new value of the atomic variable to the caller.
```

`atomic_t` data data must be accessed only through one of these functions. They will not work with normal functions that take integers. Also, make sure that atomic type values work only when the quantity in actually atomic. Operations that require multiple atomic variables still require some form of locking. For example:

```c
atomic_sub(amount, &first_atomic);
atomic_add(amount, &second_atomic);
```

There is a potential race condition between the two functions if another changes the value of `amount`. This would need additional locking to be thread safe. 

### Bit Operations

The `atomic_t` type is good for integer arithmetic, but not so much for changing individual bits. For this, the kernel provides a set of functions to modify or test single bits atomically. 

These bit operations are very fast since they work from a single machine instruction without disabling interrupts. The functions are declared in `<asm/bitops.h>` and are architecture dependent. The bad news is that data typing these functions is architecture dependent as well. Bummer. 

The available bit operations are:

```c
void set_bit(nr, void *addr);
    //Sets bit number nr in the data item pointed to by addr.
void clear_bit(nr, void *addr);
    //Clears the specified bit in the unsigned long datum that lives at addr. Its semantics are otherwise the same as set_bit.
void change_bit(nr, void *addr);
    //Toggles the bit.
test_bit(nr, void *addr);
    //This function is the only bit operation that doesn’t need to be atomic; it simply returns the current value of the bit.
int test_and_set_bit(nr, void *addr);
int test_and_clear_bit(nr, void *addr);
int test_and_change_bit(nr, void *addr);
    //Behave atomically like those listed previously, except that they also return the previous value of the bit.
```

When these functions access and modify a shared flag, you don't have to do anything except call them since they perform their operations in an atomic manner. Using bit operations to manage a lock variable that controls access to a shared variable is a little more complicated. Modern code does not do this, but it may still exist in the kernel in some ways. 

Code that needs to access a shared data item first tries to aquire a lock using either `test_and_set_bit` or `test_and_clear_bit`. Shown below is the typical implementation that assumes the lock lives at bit `nr` of address `addr`. The bit is 0 when the lock is free and 1 when the lock is busy. 

```c
/* try to set lock */
while (test_and_set_bit(nr, addr) != 0)
    wait_for_a_while( );
/* do your work */

/* release lock, and check... */
if (test_and_clear_bit(nr, addr) = = 0)
    something_went_wrong( ); /* already released: error */
```

To me it seems like spinlocks are better in just about every way. I will probably just use spinlocks over this all the time. 

### seqlocks 

The old 2.6 kernel had some "new" mechanisms to provide fast, lockless access to a shared resource. Seqlocks work in situations where the resource to be protected is small, simple, and frequently accessed, and where write access is rare but must be fast. They work by allowing readers free access to the resource but requiring those same readers to check for collisions with writers. When this collision happens, the readers retry their access. Seqlocks generally cannot be used to protect data structures involving pointers because the reader may be following a pointer that is invalid while the writer is changing the data structure.

Seqlocks are defined in `<linux/seqlock.h>` and contain the two usual methods for initialization (static and dynamic). The type is `seqlock_t`.

```c
seqlock_t lock1 = SEQLOCK_UNLOCKED;
seqlock_t lock2;

seqlock_init(&lock2);
```

The readers gain access by first obtaining an integer sequence value on entry into the critical section. On exit, that same sequence value is compared with the current value. If there is mismatch, the read access must be retried. Reader code looks like this:

```c
unsigned int seq;
do {
    seq = read_seqbegin(&the_lock);
    /* Do what you need to do */
} while read_seqretry(&the_lock, seq);
```

We use this lock to protect some sort of simple computation that requires multiple, consistent values. If you need to access the seqlock from an interrupt handler, use the following IRQ-safe versions:

```c
unsigned int read_seqbegin_irqsave(seqlock_t *lock,
    unsigned long flags);
int read_seqretry_irqrestore(seqlock_t *lock, unsigned int seq,
    unsigned long flags);
```

The exclusive writer lock can be obtained with:

```c
void write_seqlock(seqlock_t *lock);
```

This acts like a typical spinlock. To release the spinlock, use:

```c
void write_sequnlock(seqlock_t *lock);
```

The usual variants of the spinlock write functions are available for seqlock as well:

```c
void write_seqlock_irqsave(seqlock_t *lock, unsigned long flags);
void write_seqlock_irq(seqlock_t *lock);
void write_seqlock_bh(seqlock_t *lock);
int write_tryseqlock(seqlock_t *lock);

void write_sequnlock_irqrestore(seqlock_t *lock, unsigned long flags);
void write_sequnlock_irq(seqlock_t *lock);
void write_sequnlock_bh(seqlock_t *lock);
```

### Read-Copy-Update (RCU)

RCU is an advanced mutual exclusion scheme (probably outdated by now) that can yield high performance under the proper conditions. It is rarely used in drivers but worth noting here.

RCU is optimized for situations where reads are common and writes are rare (that seems to be the case with most of these mechanisms). There are a few more constraints for this mechanism though:

- Resources being protected should be accessed via pointers
- All references to those resources must be held only by atomic code
- When writing, the writing thread makes a copy, changes the copy, then aims the relevant pointer at the new version.
- When the kernel is absolutely certain that no references to the old version remain, it can be freed

Include `<linux/rcupdate.h>` to use RCUs. Reader code will look something like this:

```c
struct my_stuff *stuff;

rcu_read_lock( );
stuff = find_the_stuff(args...);
do_something_with(stuff);
rcu_read_unlock( );
```

`rcu_read_lock` is very fast. Code that executes while the read lock is held must be atomic, and no references to the protected resource can be used after the call to `rcu_read_unlock`.

Code that wants to change the protected structure needs to follow a few additional steps. It needs to:

1. Allocate a new structure
2. Copy data from the old one if needed
3. Replace the pointer used by read code

Now, any code entering the critical section sees the new version of the data. The last thing we need to do is free the old data. It can't be freed immediately because other readers may still be using a reference to older data. The code must wait until no more references exist. Since all references are atomic, once every process runs once on a processor, we can be sure that no more references exist. The RCU ends up setting aside a callback that cleans everything up once every process has been scheduled. 

The writer must get its cleanup callback by allocating a new `struct rcu_head`. But no specific initialization is needed. After a change to the resource is made, a call should be made to:

```c
void call_rcu(struct rcu_head *head, void (*func)(void *arg), void *arg);
```

The given `func` is called when it is safe to free the resource; it is passed to the same `arg` that was passed to `call_rcu`. Usually, `func` just calls `kfree`. The full RCU interface is more complex than this. If I ever need to use this I will probably need to do significantly more research on it. 









