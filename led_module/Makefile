obj-m := led_module.o

KERNELDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

default:
		$(MAKE) -C $(KERNELDIR) M=$(PWD) modules
clean:
		$(MAKE) -C $(KERNELDIR) M=$(PWD) clean
test:
		$(MAKE) -C $(KERNELDIR) M=$(PWD) modules ccflags-y="-DCONFIG_LED_MODULE_TEST"
install:
		sudo insmod led_module.ko gpio_pin=518
remove:
		sudo rmmod led_module
