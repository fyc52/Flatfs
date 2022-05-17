KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD       := $(shell pwd)

obj-m += flatfs.o #obj-m:告知Kbuild编译成.ko模块

flatfs-objs := super.o #对应上面一行，等号右侧是依赖

all:
	make -C $(KERNELDIR M=$(PWD) modules

clean:
	rm -rf *.o *.~core .depend .*.cmd *.ko *.mod.c .tmp_versions *.mod *.order *.symvers