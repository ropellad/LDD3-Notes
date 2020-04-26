### Your First Driver (have fun, get frustrated, break stuff, fix stuff)

This is my version of the hello_world that you will see for just about every programming language. It is meant to introduce the concepts of compiling, loading, and viewing a kernel module in action.

First, make sure you have the proper kernel source files on your machine:

```shell
$ sudo apt install build-essential linux-headers-$(uname -r)
```

Next, put these files into a separate directory (can be wherever). You will need `hello.c` and `Makefile`. Next, login as root using:

```shell
$ sudo -s
```

And then run:

```shell
$ make
```

Your output may look similar to this:

```shell
make -C /lib/modules/5.3.0-7648-generic/build M=/home/dom/LDD3-Notes/Code_Examples/Hello_World modules
make[1]: Entering directory '/usr/src/linux-headers-5.3.0-7648-generic'
  CC [M]  /home/dom/LDD3-Notes/Code_Examples/Hello_World/hello.o
  Building modules, stage 2.
  MODPOST 1 modules
  CC      /home/dom/LDD3-Notes/Code_Examples/Hello_World/hello.mod.o
  LD [M]  /home/dom/LDD3-Notes/Code_Examples/Hello_World/hello.ko
make[1]: Leaving directory '/usr/src/linux-headers-5.3.0-7648-generic'
```

Running a simple ls -l reveals a lot of files!

```shell
$ ls -l 
 
-rw-r--r-- 1 dom  dom  2271 Feb 12 12:20 hello.c
-rw-r--r-- 1 root root 7984 Apr 25 15:25 hello.ko
-rw-r--r-- 1 root root   56 Apr 25 15:25 hello.mod
-rw-r--r-- 1 root root  646 Apr 25 15:25 hello.mod.c
-rw-r--r-- 1 root root 2800 Apr 25 15:25 hello.mod.o
-rw-r--r-- 1 root root 6072 Apr 25 15:25 hello.o
-rw-r--r-- 1 dom  dom   378 Feb 12 12:20 Makefile
-rw-r--r-- 1 root root   56 Apr 25 15:25 modules.order
-rw-r--r-- 1 root root    0 Apr 25 15:25 Module.symvers
```

The one we are interested in now is called `hello.ko`. This is our kernel object. Now we can load it into the kernel with:

```shell
$ insmod hello.ko
```

Now, in another terminal, we can look at the output of the module to the kernel log with:

```shell
$ dmesg -wH

[Apr25 17:27] Hello, world 
              ============= 
[  +0.000004] short integer: 1
[  +0.000002] integer: 56
[  +0.000002] long integer: 1900
[  +0.000002] string: test string
[  +0.000002] intArray[0] = 1
[  +0.000001] intArray[1] = 2
[  +0.000002] intArray[2] = 3
[  +0.000001] intArray[3] = 4
[  +0.000002] intArray[4] = 5
[  +0.000001] intArray[5] = -8
[  +0.000002] got 0 arguments for intArray.
```

The module can be removed with:

```shell
$ rmmod hello 
```

And the resulting output in dmesg will look like:

```shell
[Apr25 18:14] Goodbye, cruel world!
```

Now for some fun - passing command line arguments to a module:

```shell
$ insmod hello.ko short_int=4 reg_int=5 long_int=56789 sample_string="The_answer_is_42" intArray=9,9,9
```


```shell
[  +5.091416] Hello, world 
              ============= 
[  +0.000004] short integer: 4
[  +0.000002] integer: 5
[  +0.000002] long integer: 56789
[  +0.000002] string: The_answer_is_42
[  +0.000002] intArray[0] = 9
[  +0.000002] intArray[1] = 9
[  +0.000001] intArray[2] = 9
[  +0.000002] intArray[3] = 4
[  +0.000002] intArray[4] = 5
[  +0.000002] intArray[5] = -8
[  +0.000002] got 3 arguments for intArray.
```
