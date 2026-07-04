obj-m := steering_module.o

KERNELDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

default:
		$(MAKE) -C $(KERNELDIR) M=$(PWD) modules
clean:
		$(MAKE) -C $(KERNELDIR) M=$(PWD) clean
test:
		$(MAKE) -C $(KERNELDIR) M=$(PWD) modules ccflags-y="-DCONFIG_STEERING_MODULE_TEST"
install:
		sudo insmod steering_module.ko left_motor_pin=530 right_motor_pin=531 left_endstop_pin=535 right_endstop_pin=537
remove:
		sudo rmmod steering_module
