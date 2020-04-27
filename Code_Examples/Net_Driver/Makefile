MODULE_NAME := domnetfilter
obj-m := $(MODULE_NAME).o

OBJ_LIST := filter.o
$(MODULE_NAME)-y += $(OBJ_LIST)

ccflags-y := -O2

KERNELDIR := /lib/modules/$(shell uname -r)/build

all: filter

filter:
	make -C $(KERNELDIR) M=$(PWD) modules
