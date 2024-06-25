# Makefile for compiling the test_data_preprocessing, test_tensorflow_lite_kernel_interpreter, and flax_kernel_interpreter kernel modules

obj-m += test_data_preprocessing.o
obj-m += test_tensorflow_lite_kernel_interpreter.o
obj-m += flax_kernel_interpreter.o

# Add include path for header files
EXTRA_CFLAGS += -I$(PWD)/../include

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
