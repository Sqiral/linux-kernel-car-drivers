obj-m := motors_module.o

KERNELDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

default:
		$(MAKE) -C $(KERNELDIR) M=$(PWD) modules
install:
		sudo insmod motors_module.ko left_fwd_pin=519 left_bwd_pin=520 right_fwd_pin=522 right_bwd_pin=523
test:
		$(MAKE) -C $(KERNELDIR) M=$(PWD) modules ccflags-y="-DCONFIG_MOTORS_MODULE_TEST"
remove:
		sudo rmmod motors_module
clean:
		$(MAKE) -C $(KERNELDIR) M=$(PWD) clean
