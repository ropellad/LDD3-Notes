// Code to test sending manual messages over a virtual can interface

// Use these headers to communicate with a raw can data frame
#include <linux/can.h>
#include <linux/can/raw.h>

// Additional socket stuff needed by the socketCAN library
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>

// And finally, the regular c types for having some fun!
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char *argv[])
{
	int fd; //file descriptor for reading and writing
	int bytes_transferred; //number of bytes sent over CAN
	
	// structure for CAN sockets includes:
	// address family, interface index, specific address info
	struct sockaddr_can addr; 
	
	// structure for an individual frame in CAN
	struct can_frame frame; 
	
	// Linux supports some standard ioctls to configure network devices.
	// They can be used on any socket's file descriptor regardless of the
	// family or type.  Most of them pass an ifreq structure:
	struct ifreq ifr;

	// Check to make sure we have entered a CAN interface to use
	if (argc < 2)
	{
		printf("Please enter CAN bus to write to (ex: vcan0)\n");
		printf("Args: Canbus\n");
		return -1;
	}

	// Get the interface name from command line arguments
	const char *ifname = argv[1];

	// Now we create a new socket endpoint for communication
	// Args: domain, type, protocol
	// returns a file descriptor 
	if((fd = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
		perror("Error while opening socket");
		return -2;
	}

	// Here we pass the ifreq structure to ioctl
	strcpy(ifr.ifr_name, ifname);
	ioctl(fd, SIOCGIFINDEX, &ifr);
	
	// Set address family and network interface index of CAN socket
	addr.can_family  = AF_CAN;
	addr.can_ifindex = ifr.ifr_ifindex;

	// Print the name of the interface and the index
	printf("%s at index %d\n", ifname, ifr.ifr_ifindex);

	// Bind the file descriptor name to the socket
	if(bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("Error in socket bind");
		return -3;
	}

	// Setup the can frame with the correct ID, DLC, and values
	frame.can_id  = 0x123; // 32-bit CAN_ID + EFF/RTR/ERR flags
	frame.can_dlc = 8; //data length code - we want 8 bytes here (the MAX!)
	frame.data[0] = 0xde; //first data byte
	frame.data[1] = 0xad; //second data byte
	frame.data[2] = 0xbe; //third data byte
	frame.data[3] = 0xef; //fourth data byte ...
	frame.data[4] = 0x12; 
	frame.data[5] = 0x34; 
	frame.data[6] = 0x56; 
	frame.data[7] = 0x78; 

	// Write the data to the CAN bus and record the number of bytes written
	bytes_transferred = write(fd, &frame, sizeof(struct can_frame));

	printf("Wrote %d bytes\n", bytes_transferred);
	
	return 0;
}
