# Networking Fun
#### No, not the kind where you have to meet a lot of people

If you want to be really mean to one of your roomates - here is a way to disable all TCP connections on a computer at the kernel level. Start by downloading the two source files, running `make` and inserting the module into the kernel. The module has one input parameter that will enable/disable TCP blocking. First, let's do the normal case:

```shell
$ insmod domnetfilter.ko 
```

With corresponding output in dmesg as:

```shell
[Apr26 19:22] domnetfilter: Doms Network Filter Started!
[  +0.000004] domnetfilter: Major number 510
[ +13.188771] domnetfilter: TCP connection initiated from 192.168.1.XXX:XXXXX
[  +0.000003] domnetfilter: Packets allowed to pass!
```

- I replaced the exact IP address with X's to denote that these will be different for your machine
- Notice: the module says "Packets allowed to pass!"
  - Blocking has not been enabled yet

Next, we are going to have some fun. To block all TCP, run the module with the following input:

```shell
insmod domnetfilter.ko toggle_string="block"
```

Now you will see the following in dmesg:

```shell
[  +0.447914] domnetfilter: TCP connection initiated from 192.168.1.XXX:XXXXX
[  +0.000047] domnetfilter: Packets being blocked!
```

Try and browse the web. Nothing should happen. You will need to run the module again without the blocking flag set to restore TCP packets being delivered. 

Have fun, and don't use this for (pure) evil. 
