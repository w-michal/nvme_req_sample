obj-m += nvme_req_sample.o
ccflags-y := -std=gnu99 -Wno-declaration-after-statement
all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
