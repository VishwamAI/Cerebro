# Makefile for compiling the test_data_preprocessing kernel module

obj-m += test_data_preprocessing.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
