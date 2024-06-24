#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

#define DEVICE_NAME "flax_device"
#define CLASS_NAME "flax"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("kasinadhsarma, Devin");
MODULE_DESCRIPTION("A kernel module for executing Flax models");
MODULE_VERSION("0.1");

static int major_number;
static char *kernel_buffer;
static struct class *flax_class = NULL;
static struct device *flax_device = NULL;

static int dev_open(struct inode *inodep, struct file *filep) {
    printk(KERN_INFO "FlaxDevice: Device opened\n");
    return 0;
}

static int dev_release(struct inode *inodep, struct file *filep) {
    printk(KERN_INFO "FlaxDevice: Device closed\n");
    return 0;
}

static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset) {
    int error_count = 0;
    error_count = copy_to_user(buffer, kernel_buffer, len);

    if (error_count == 0) {
        printk(KERN_INFO "FlaxDevice: Sent %zu characters to the user\n", len);
        return 0;
    } else {
        printk(KERN_INFO "FlaxDevice: Failed to send %d characters to the user\n", error_count);
        return -EFAULT;
    }
}

static ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset) {
    sprintf(kernel_buffer, "%s(%zu letters)", buffer, len);
    printk(KERN_INFO "FlaxDevice: Received %zu characters from the user\n", len);

    // Command handling logic
    if (strncmp(buffer, "LOAD_MODEL", 10) == 0) {
        // Handle model loading
        printk(KERN_INFO "FlaxDevice: Loading model\n");
        // Add model loading logic here
    } else if (strncmp(buffer, "EXECUTE_MODEL", 13) == 0) {
        // Handle model execution
        printk(KERN_INFO "FlaxDevice: Executing model\n");
        // Add model execution logic here
    } else if (strncmp(buffer, "GET_RESULTS", 11) == 0) {
        // Handle retrieving results
        printk(KERN_INFO "FlaxDevice: Retrieving results\n");
        // Add result retrieval logic here
    } else {
        printk(KERN_INFO "FlaxDevice: Unknown command\n");
    }

    return len;
}

static struct file_operations fops = {
    .open = dev_open,
    .read = dev_read,
    .write = dev_write,
    .release = dev_release,
};

static int __init flax_device_init(void) {
    printk(KERN_INFO "FlaxDevice: Initializing the FlaxDevice\n");

    major_number = register_chrdev(0, DEVICE_NAME, &fops);
    if (major_number < 0) {
        printk(KERN_ALERT "FlaxDevice failed to register a major number\n");
        return major_number;
    }
    printk(KERN_INFO "FlaxDevice: Registered correctly with major number %d\n", major_number);

    flax_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(flax_class)) {
        unregister_chrdev(major_number, DEVICE_NAME);
        printk(KERN_ALERT "Failed to register device class\n");
        return PTR_ERR(flax_class);
    }
    printk(KERN_INFO "FlaxDevice: Device class registered correctly\n");

    flax_device = device_create(flax_class, NULL, MKDEV(major_number, 0), NULL, DEVICE_NAME);
    if (IS_ERR(flax_device)) {
        class_destroy(flax_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        printk(KERN_ALERT "Failed to create the device\n");
        return PTR_ERR(flax_device);
    }
    printk(KERN_INFO "FlaxDevice: Device class created correctly\n");

    kernel_buffer = kmalloc(1024, GFP_KERNEL);
    if (!kernel_buffer) {
        device_destroy(flax_class, MKDEV(major_number, 0));
        class_destroy(flax_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        printk(KERN_ALERT "Failed to allocate memory for kernel buffer\n");
        return -ENOMEM;
    }

    return 0;
}

static void __exit flax_device_exit(void) {
    kfree(kernel_buffer);
    device_destroy(flax_class, MKDEV(major_number, 0));
    class_unregister(flax_class);
    class_destroy(flax_class);
    unregister_chrdev(major_number, DEVICE_NAME);
    printk(KERN_INFO "FlaxDevice: Goodbye from the FlaxDevice\n");
}

module_init(flax_device_init);
module_exit(flax_device_exit);
