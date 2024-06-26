#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/fs_struct.h>
#include <linux/err.h>
#include <flatbuffers/flatbuffers.h>
#include "schema_v3c_generated.h"

#define DEVICE_NAME "tensorflow_model_loader_device"
#define CLASS_NAME "tensorflow_model_loader"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("kasinadhsarma, Devin");
MODULE_DESCRIPTION("A kernel module for loading and parsing TensorFlow models");
MODULE_VERSION("0.1");

static int major_number;
static char *kernel_buffer;
static struct class *tensorflow_model_loader_class = NULL;
static struct device *tensorflow_model_loader_device = NULL;

static int dev_open(struct inode *inodep, struct file *filep) {
    printk(KERN_INFO "TFModelLoaderDevice: Device opened\n");
    return 0;
}

static int dev_release(struct inode *inodep, struct file *filep) {
    printk(KERN_INFO "TFModelLoaderDevice: Device closed\n");
    return 0;
}

static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset) {
    int error_count = 0;
    error_count = copy_to_user(buffer, kernel_buffer, len);

    if (error_count == 0) {
        printk(KERN_INFO "TFModelLoaderDevice: Sent %zu characters to the user\n", len);
        return 0;
    } else {
        printk(KERN_INFO "TFModelLoaderDevice: Failed to send %d characters to the user\n", error_count);
        return -EFAULT;
    }
}

static ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset) {
    snprintf(kernel_buffer, 1024, "%s(%zu letters)", buffer, len);
    printk(KERN_INFO "TFModelLoaderDevice: Received %zu characters from the user\n", len);

    // Command handling logic
    if (strncmp(buffer, "LOAD_MODEL", 10) == 0) {
        // Handle model loading
        printk(KERN_INFO "TFModelLoaderDevice: Loading model\n");
        int ret = load_model(kernel_buffer);
        if (ret < 0) {
            printk(KERN_ALERT "TFModelLoaderDevice: Failed to load model\n");
        }
    } else {
        printk(KERN_INFO "TFModelLoaderDevice: Unknown command\n");
    }

    return len;
}

// Function to load the TensorFlow model into kernel memory
static int load_model(const char *model_path) {
    struct file *file;
    mm_segment_t old_fs;
    loff_t pos = 0;
    loff_t file_size;
    int ret;

    // Open the model file
    old_fs = get_fs();
    set_fs(KERNEL_DS);
    file = filp_open(model_path, O_RDONLY, 0);
    set_fs(old_fs);

    if (IS_ERR(file)) {
        printk(KERN_ALERT "TensorFlowInterpreterDevice: Failed to open model file\n");
        return PTR_ERR(file);
    }

    // Get the size of the model file
    old_fs = get_fs();
    set_fs(KERNEL_DS);
    file_size = vfs_llseek(file, 0, SEEK_END);
    vfs_llseek(file, 0, SEEK_SET);
    set_fs(old_fs);

    if (file_size < 0) {
        filp_close(file, NULL);
        printk(KERN_ALERT "TensorFlowInterpreterDevice: Failed to get model file size\n");
        return file_size;
    }

    // Allocate memory for the model data
    kernel_buffer = kmalloc(file_size, GFP_KERNEL);
    if (!kernel_buffer) {
        filp_close(file, NULL);
        printk(KERN_ALERT "TensorFlowInterpreterDevice: Failed to allocate memory for model data\n");
        return -ENOMEM;
    }

    // Read the model file into the kernel buffer
    old_fs = get_fs();
    set_fs(KERNEL_DS);
    ret = kernel_read(file, kernel_buffer, file_size, &pos);
    set_fs(old_fs);

    if (ret < 0) {
        kfree(kernel_buffer);
        filp_close(file, NULL);
        printk(KERN_ALERT "TensorFlowInterpreterDevice: Failed to read model file\n");
        return ret;
    }

    // Close the model file
    filp_close(file, NULL);

    printk(KERN_INFO "TensorFlowInterpreterDevice: Model loaded successfully\n");
    return 0;
}

// Function to parse the TensorFlow model data
static int parse_tensorflow_model(char *model_data) {
    // Create a FlatBuffer verifier to verify the model data
    flatbuffers::Verifier verifier((const uint8_t *)model_data, strlen(model_data));
    if (!tflite::VerifyModelBuffer(verifier)) {
        printk(KERN_ALERT "TensorFlowInterpreterDevice: Invalid TensorFlow model data\n");
        return -EINVAL;
    }

    // Get the root of the FlatBuffer model
    const tflite::Model *model = tflite::GetModel(model_data);
    if (model == NULL) {
        printk(KERN_ALERT "TensorFlowInterpreterDevice: Failed to get TensorFlow model root\n");
        return -EINVAL;
    }

    // Iterate over the subgraphs in the model
    for (int i = 0; i < model->subgraphs()->size(); i++) {
        const tflite::SubGraph *subgraph = model->subgraphs()->Get(i);
        printk(KERN_INFO "TensorFlowInterpreterDevice: Subgraph %d has %d operators\n", i, subgraph->operators()->size());

        // Iterate over the operators in the subgraph
        for (int j = 0; j < subgraph->operators()->size(); j++) {
            const tflite::Operator *op = subgraph->operators()->Get(j);
            printk(KERN_INFO "TensorFlowInterpreterDevice: Operator %d has opcode %d\n", j, op->opcode_index());
        }
    }

    printk(KERN_INFO "TensorFlowInterpreterDevice: Model parsed successfully\n");
    return 0;
}

static struct file_operations fops = {
    .open = dev_open,
    .read = dev_read,
    .write = dev_write,
    .release = dev_release,
};

static int __init tensorflow_model_loader_device_init(void) {
    printk(KERN_INFO "TFModelLoaderDevice: Initializing the TFModelLoaderDevice\n");

    major_number = register_chrdev(0, DEVICE_NAME, &fops);
    if (major_number < 0) {
        printk(KERN_ALERT "TFModelLoaderDevice failed to register a major number\n");
        return major_number;
    }
    printk(KERN_INFO "TFModelLoaderDevice: Registered correctly with major number %d\n", major_number);

    tensorflow_model_loader_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(tensorflow_model_loader_class)) {
        unregister_chrdev(major_number, DEVICE_NAME);
        printk(KERN_ALERT "Failed to register device class\n");
        return PTR_ERR(tensorflow_model_loader_class);
    }
    printk(KERN_INFO "TFModelLoaderDevice: Device class registered correctly\n");

    tensorflow_model_loader_device = device_create(tensorflow_model_loader_class, NULL, MKDEV(major_number, 0), NULL, DEVICE_NAME);
    if (IS_ERR(tensorflow_model_loader_device)) {
        class_destroy(tensorflow_model_loader_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        printk(KERN_ALERT "Failed to create the device\n");
        return PTR_ERR(tensorflow_model_loader_device);
    }
    printk(KERN_INFO "TFModelLoaderDevice: Device class created correctly\n");

    kernel_buffer = kmalloc(1024, GFP_KERNEL);
    if (!kernel_buffer) {
        device_destroy(tensorflow_model_loader_class, MKDEV(major_number, 0));
        class_destroy(tensorflow_model_loader_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        printk(KERN_ALERT "Failed to allocate memory for kernel buffer\n");
        return -ENOMEM;
    }

    return 0;
}

static void __exit tensorflow_model_loader_device_exit(void) {
    kfree(kernel_buffer);
    device_destroy(tensorflow_model_loader_class, MKDEV(major_number, 0));
    class_unregister(tensorflow_model_loader_class);
    class_destroy(tensorflow_model_loader_class);
    unregister_chrdev(major_number, DEVICE_NAME);
    printk(KERN_INFO "TFModelLoaderDevice: Goodbye from the TFModelLoaderDevice\n");
}

module_init(tensorflow_model_loader_device_init);
module_exit(tensorflow_model_loader_device_exit);
