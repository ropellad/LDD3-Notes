### Alright this is a bit more than Hello_world

First, take a scroll through the source file. It has many comments!

First, make main.ko from the source and makefile. When you insert the module you should see this output:

```shell
[Apr25 20:52] Successfully started DOMCHARDEV-(237, 0)
[  +0.000094] Successfully started DOMCHARDEV-(237, 1)
[  +0.000101] Successfully started DOMCHARDEV-(237, 2)
[  +0.000081] Successfully started DOMCHARDEV-(237, 3)
```

One thing you can do is read from the devices:

```
$ head -1 /dev/domchardev-0

Hello from the kernel world! This is Dom's char device!
```

And corresponding dmesg output:

```shell
[  +4.833332] DOMCHARDEV-0: Device open
[  +0.000012] Reading device number: (237, 0)
[  +0.000031] DOMCHARDEV-0: Device close
```

Another thing you can do is write to the device:

```shell
$ echo "hi there!" > /dev/domchardev-0
```

And corresponding dmesg output:

```shell
[ +15.280692] DOMCHARDEV-0: Device open
[  +0.000027] Writing device number: (237, 0)
[  +0.000002] Copied 10 bytes from the user
[  +0.000003] Data from the user: hi there!
[  +0.000008] DOMCHARDEV-0: Device close
```

You can also access the uevent structure:

```shell
$ cat /sys/dev/char/237\:0/uevent # replace with your device major/minor numbers!

MAJOR=237
MINOR=0
DEVNAME=domchardev-0
DEVMODE=0666
```
