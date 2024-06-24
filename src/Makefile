# Makefile for compiling the test_data_preprocessing and test_tensorflow_lite_kernel_interpreter kernel modules

obj-m += test_data_preprocessing.o
obj-m += test_tensorflow_lite_kernel_interpreter.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
