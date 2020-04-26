# Playing with SocketCAN

Alright folks, some bonus content for you.

While this section is not specifically a kernel module, it is a user-space program set that heavily uses the Linux SocketCAN API (which is a lot of kernel code relating to networking). This is like 1 step above kernel programming so I wanted to include it here.

First, you will need the two source files: ReceiveRawPacket.c and SendRawPacket.c. The first file receives CAN data continuously, and the second file sends a one-time CAN message. 

Start by compiling both programs with:

```shell
$ gcc -o CanReceive ReceiveRawPacket.c
$ gcc -o CanSend SendRawPacket.c
```

Now, you will need to setup a virtual CAN bus, called vcan0:

```shell
$ modprobe can
$ modprobe can_raw
$ modprobe vcan
$ sudo ip link add dev vcan0 type vcan
$ sudo ip link set up vcan0
$ ip link show vcan0
```

Start by executing CanReceive and then CanSend. You should see this from the CanSend terminal:

```shell
$ ./CanSend vcan0

vcan0 at index 4
Wrote 16 bytes
```

And see this from the CanReceive terminal output:

```shell
$ ./CanReceive vcan0
vcan0 at index 4
Read 16 bytes
CAN ID: 123 
CAN DLC: 8 
CAN DATA: 
de ad be ef 12 34 56 78 
```

The CanReceive file will keep listening for more data, while the CanSend will only send once. You can run the CanSend file multiple times to see data sent multiple times. You can also monitor the CAN bus with a built-in tool called candump:

```shell
$ candump vcan0

vcan0  123   [8]  DE AD BE EF 12 34 56 78
```
