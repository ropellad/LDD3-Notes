MODULE_NAME := domsblockdev
obj-m := $(MODULE_NAME).o

OBJ_LIST := domblockdev.o
$(MODULE_NAME)-y += $(OBJ_LIST)

ccflags-y := -O2

KERNELDIR := /lib/modules/$(shell uname -r)/build

all: domblockdev

domblockdev:
	make -C $(KERNELDIR) M=$(PWD) modules
