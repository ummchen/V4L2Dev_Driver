CONFIG_MODULE_SIG=n

PWD         := $(shell pwd) 
KVERSION    := $(shell uname -r)
#KERNEL_DIR   = /usr/src/linux-headers-$(KVERSION)/	#for Ubuntu
KERNEL_DIR   = /usr/lib/modules/$(KVERSION)/build/	#for ArchLinux

MODULE_NAME  = v4l2dev
obj-m       := $(MODULE_NAME).o   

all:
	make -C $(KERNEL_DIR) M=$(PWD) modules
clean:
	make -C $(KERNEL_DIR) M=$(PWD) clean

