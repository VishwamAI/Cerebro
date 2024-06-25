#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/sysinfo.h>
#include <linux/mm.h>
#include "model_interpreter.h"

#define DEVICE_NAME "tensorflow_lite_kernel_interpreter"
#define CLASS_NAME "tensorflow_lite"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("kasinadhsarma, Devin");
MODULE_DESCRIPTION("A kernel module for interpreting and executing TensorFlow Lite models");
MODULE_VERSION("0.1");

struct tensorflow_lite_model {
    void *graph;
    uint64_t parameters;
};

int load_model(const char *model_path);
int execute_model(void);
static int get_results(char *result_buffer, size_t buffer_size);
static int interpret_and_execute_model(char *model_data);
static struct tensorflow_lite_model *parse_tensorflow_lite_model(char *model_data);
static int load_computation_graph(struct tensorflow_lite_model *model);
static int execute_computation_graph(struct tensorflow_lite_model *model);

static int major_number;
static char *kernel_buffer = NULL;
static DEFINE_MUTEX(kernel_buffer_mutex);

static int dev_open(struct inode *inodep, struct file *filep) {
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Device opened\n");
    return 0;
}

static int dev_release(struct inode *inodep, struct file *filep) {
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Device closed\n");
    if (kernel_buffer) {
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Freeing kernel buffer at %p\n", kernel_buffer);
        kfree(kernel_buffer);
        kernel_buffer = NULL;
    }
    mutex_unlock(&kernel_buffer_mutex);
    return 0;
}

static ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset) {
    int ret;
    char *result_buffer = NULL;
    char *temp_buffer = NULL;
    int error_count;

    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Entering dev_write function\n");
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: buffer=%p, len=%zu\n", buffer, len);

    if (len > 1023) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Input length exceeds buffer limit\n");
        return -EINVAL;
    }

    if (!access_ok(buffer, len)) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: User space buffer is not accessible\n");
        return -EFAULT;
    }

    if (!kernel_buffer) {
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Allocating kernel buffer\n");
        kernel_buffer = kmalloc(1024, GFP_KERNEL);
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: kernel_buffer allocated at %p\n", kernel_buffer);
        if (!kernel_buffer) {
            printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Failed to allocate kernel buffer\n");
            return -ENOMEM;
        }
    }

    mutex_lock(&kernel_buffer_mutex);

    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Before memcpy, buffer: %s\n", buffer);
    if (copy_from_user(kernel_buffer, buffer, len)) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Failed to copy data from user space\n");
        mutex_unlock(&kernel_buffer_mutex);
        return -EFAULT;
    }
    kernel_buffer[len] = '\0'; // Ensure null-termination
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: After memcpy, kernel_buffer: %s\n", kernel_buffer);

    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Before kmalloc for result_buffer\n");
    result_buffer = kmalloc(len + 1, GFP_KERNEL); // Dynamically allocate memory based on actual size
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: result_buffer allocated at %p\n", result_buffer);
    if (!result_buffer) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Failed to allocate memory for result buffer\n");
        mutex_unlock(&kernel_buffer_mutex);
        return -ENOMEM;
    }
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Before kmalloc for temp_buffer\n");
    temp_buffer = kmalloc(len + 1, GFP_KERNEL); // Dynamically allocate memory based on actual size
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: temp_buffer allocated at %p\n", temp_buffer);
    if (!temp_buffer) {
        kfree(result_buffer);
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Failed to allocate memory for temp buffer\n");
        mutex_unlock(&kernel_buffer_mutex);
        return -ENOMEM;
    }

    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: result_buffer allocated at %p, temp_buffer allocated at %p\n", result_buffer, temp_buffer);

    ret = execute_model();
    if (ret < 0) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Model execution failed\n");
        kfree(result_buffer);
        kfree(temp_buffer);
        mutex_unlock(&kernel_buffer_mutex);
        return ret;
    }

    ret = get_results(result_buffer, len + 1);
    if (ret < 0) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Failed to retrieve results\n");
        kfree(result_buffer);
        kfree(temp_buffer);
        mutex_unlock(&kernel_buffer_mutex);
        return ret;
    } else {
        result_buffer[len] = '\0'; // Ensure null-termination
        if (strnlen(result_buffer, len) >= len) {
            printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: result_buffer is too large\n");
            kfree(result_buffer);
            kfree(temp_buffer);
            mutex_unlock(&kernel_buffer_mutex);
            return -EINVAL;
        }
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Before strncpy, result_buffer: %s\n", result_buffer);
        strncpy(temp_buffer, result_buffer, len);
        temp_buffer[len] = '\0'; // Ensure null-termination
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: After strncpy, temp_buffer: %s\n", temp_buffer);

        if (strnlen(temp_buffer, len) >= len) {
            printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: User buffer size exceeded\n");
            kfree(result_buffer);
            kfree(temp_buffer);
            mutex_unlock(&kernel_buffer_mutex);
            return -EINVAL;
        }

        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Before copy_to_user\n");
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: temp_buffer: %s, len: %zu, temp_buffer size: %zu\n", temp_buffer, len, len + 1);
        error_count = copy_to_user((void *)buffer, temp_buffer, strnlen(temp_buffer, len) + 1); // Ensure null-terminator is copied
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: After copy_to_user, error_count: %d\n", error_count);
        if (error_count != 0) {
            printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Failed to copy results to user space\n");
            kfree(result_buffer);
            kfree(temp_buffer);
            mutex_unlock(&kernel_buffer_mutex);
            return -EFAULT;
        }
    }
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Freeing result_buffer at %p and temp_buffer at %p\n", result_buffer, temp_buffer);
    kfree(result_buffer);
    kfree(temp_buffer);
    mutex_unlock(&kernel_buffer_mutex);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Exiting dev_write function\n");
    return strnlen(kernel_buffer, 1024);
}

static void __exit tensorflow_lite_kernel_interpreter_exit(void) {
    if (kernel_buffer) {
        kfree(kernel_buffer);
    }
    unregister_chrdev(major_number, DEVICE_NAME);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Goodbye from the TensorFlowLiteKernelInterpreter\n");
}

static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset) {
    int error_count = 0;
    error_count = copy_to_user(buffer, kernel_buffer, len);

    if (error_count == 0) {
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Sent %zu characters to the user\n", len);
        return len;
    } else {
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Failed to send %d characters to the user\n", error_count);
        return -EFAULT;
    }
}

// Function to load the model from the specified path into kernel memory
int load_model(const char *model_path) {
    struct file *model_file;
    loff_t pos = 0;
    int ret;
    struct sysinfo mem_info;
    loff_t file_size;

    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Entering load_model function\n");
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: model_path: %s\n", model_path);

    // Log system memory usage before loading the model
    si_meminfo(&mem_info);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Memory before loading model - Total: %lu, Free: %lu, Available: %lu\n",
           mem_info.totalram, mem_info.freeram, mem_info.freeram + mem_info.bufferram);

    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Attempting to open model file %s\n", model_path);
    model_file = filp_open(model_path, O_RDONLY, 0);
    if (IS_ERR(model_file)) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Failed to open model file %s\n", model_path);
        return PTR_ERR(model_file);
    }
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Successfully opened model file %s\n", model_path);

    // Determine the size of the model file
    file_size = i_size_read(file_inode(model_file));
    if (file_size > 1024) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Model file size exceeds buffer limit\n");
        filp_close(model_file, NULL);
        return -ENOMEM;
    }

    // Free existing kernel_buffer if already allocated
    if (kernel_buffer) {
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Freeing existing kernel buffer at %p\n", kernel_buffer);
        kfree(kernel_buffer);
    }

    // Allocate memory for kernel_buffer based on the model size
    kernel_buffer = kmalloc(file_size, GFP_KERNEL);
    if (!kernel_buffer) {
        filp_close(model_file, NULL);
        printk(KERN_ALERT "Failed to allocate memory for kernel buffer\n");
        return -ENOMEM;
    }

    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Attempting to read model file into kernel buffer\n");
    // Read the model file into kernel_buffer
    ret = kernel_read(model_file, kernel_buffer, file_size, &pos);
    if (ret < 0) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Failed to read model file %s\n", model_path);
        kfree(kernel_buffer);
        filp_close(model_file, NULL);
        return ret;
    }
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Successfully read model file into kernel buffer\n");

    filp_close(model_file, NULL);

    // Log system memory usage after loading the model
    si_meminfo(&mem_info);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Memory after loading model - Total: %lu, Free: %lu, Available: %lu\n",
           mem_info.totalram, mem_info.freeram, mem_info.freeram + mem_info.bufferram);

    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Model loaded from %s\n", model_path);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: kernel_buffer after loading model: %s\n", kernel_buffer);
    return 0;
}

int execute_model(void) {
    int ret;
    struct sysinfo mem_info;

    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Entering execute_model function\n");
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: kernel_buffer before execution: %s\n", kernel_buffer);

    // Log system memory usage before executing the model
    si_meminfo(&mem_info);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Memory before executing model - Total: %lu, Free: %lu, Available: %lu\n",
           mem_info.totalram, mem_info.freeram, mem_info.freeram + mem_info.bufferram);

    ret = interpret_and_execute_model(kernel_buffer);

    if (ret < 0) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Model execution failed with error code %d\n", ret);
        return ret;
    }

    snprintf(kernel_buffer, 1024, "Model execution result: %d", ret);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Model executed\n");
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: kernel_buffer after execution: %s\n", kernel_buffer);

    // Log system memory usage after executing the model
    si_meminfo(&mem_info);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Memory after executing model - Total: %lu, Free: %lu, Available: %lu\n",
           mem_info.totalram, mem_info.freeram, mem_info.freeram + mem_info.bufferram);

    return ret;
}

static int interpret_and_execute_model(char *model_data) {
    int result = 0;
    struct tensorflow_lite_model *model = parse_tensorflow_lite_model(model_data);
    int ret;
    if (!model) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Failed to parse model data\n");
        return -EINVAL;
    }
    ret = load_computation_graph(model);
    if (ret < 0) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Failed to load computation graph\n");
        kfree(model);
        return ret;
    }
    result = execute_computation_graph(model);
    kfree(model->graph);
    kfree(model);
    return result;
}

static struct tensorflow_lite_model *parse_tensorflow_lite_model(char *model_data) {
    struct tensorflow_lite_model *model = kmalloc(sizeof(struct tensorflow_lite_model), GFP_KERNEL);
    size_t offset = 0;
    if (!model) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Failed to allocate memory for model\n");
        return NULL;
    }
    while (offset < strlen(model_data)) {
        uint8_t field_number = model_data[offset] >> 3;
        uint8_t wire_type = model_data[offset] & 0x07;
        offset++;
        switch (field_number) {
            case 1:
                if (wire_type == 2) {
                    uint32_t length = 0;
                    memcpy(&length, &model_data[offset], sizeof(uint32_t));
                    offset += sizeof(uint32_t);
                    model->graph = kmalloc(length, GFP_KERNEL);
                    if (!model->graph) {
                        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Failed to allocate memory for graph\n");
                        kfree(model);
                        return NULL;
                    }
                    memcpy(model->graph, &model_data[offset], length);
                    offset += length;
                }
                break;
            case 2:
                if (wire_type == 0) {
                    uint64_t value = 0;
                    memcpy(&value, &model_data[offset], sizeof(uint64_t));
                    offset += sizeof(uint64_t);
                    model->parameters = value;
                }
                break;
            default:
                printk(KERN_WARNING "TensorFlowLiteKernelInterpreter: Unknown field number %u\n", field_number);
                break;
        }
    }
    return model;
}

static int load_computation_graph(struct tensorflow_lite_model *model) {
    if (!model || !model->graph) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Invalid model or graph\n");
        return -EINVAL;
    }
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Computation graph loaded\n");
    return 0;
}

static int execute_computation_graph(struct tensorflow_lite_model *model) {
    int result = 0;
    uint8_t *graph_data;
    size_t offset;

    if (!model || !model->graph) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Invalid model or graph\n");
        return -EINVAL;
    }

    // Implement the actual logic for executing the computation graph
    // For simplicity, assume the graph is a sequence of operations that can be executed in order
    // Iterate through the graph and execute each operation
    graph_data = (uint8_t *)model->graph;
    offset = 0;
    while (offset < model->parameters) {
        uint8_t operation = graph_data[offset];
        offset++;
        switch (operation) {
            case 0x01: // Example operation code
                // Perform the operation (placeholder)
                result += graph_data[offset];
                offset++;
                break;
            case 0x02: // Another example operation code
                // Perform the operation (placeholder)
                result *= graph_data[offset];
                offset++;
                break;
            default:
                printk(KERN_WARNING "TensorFlowLiteKernelInterpreter: Unknown operation code %u\n", operation);
                break;
        }
    }

    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Computation graph executed with result %d\n", result);
    return result;
}

static int get_results(char *result_buffer, size_t buffer_size) {
    int snprintf_ret;
    struct sysinfo mem_info;

    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Entering get_results function\n");
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: kernel_buffer before retrieving results: %s\n", kernel_buffer);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: result_buffer before retrieving results: %s\n", result_buffer);

    // Log system memory usage before retrieving results
    si_meminfo(&mem_info);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Memory before retrieving results - Total: %lu, Free: %lu, Available: %lu\n",
           mem_info.totalram, mem_info.freeram, mem_info.freeram + mem_info.bufferram);

    snprintf_ret = snprintf(result_buffer, buffer_size, "%s", kernel_buffer);
    if (snprintf_ret >= buffer_size) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: snprintf output was truncated\n");
        return -EINVAL;
    }

    // Log system memory usage after retrieving results
    si_meminfo(&mem_info);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Memory after retrieving results - Total: %lu, Free: %lu, Available: %lu\n",
           mem_info.totalram, mem_info.freeram, mem_info.freeram + mem_info.bufferram);

    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Results retrieved\n");
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: result_buffer after retrieving results: %s\n", result_buffer);
    return 0;
}

static struct file_operations fops = {
    .open = dev_open,
    .read = dev_read,
    .write = dev_write,
    .release = dev_release,
};

static int __init tensorflow_lite_kernel_interpreter_init(void) {
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Initializing the TensorFlowLiteKernelInterpreter\n");

    major_number = register_chrdev(0, DEVICE_NAME, &fops);
    if (major_number < 0) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter failed to register a major number\n");
        return major_number;
    }
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Registered correctly with major number %d\n", major_number);

    return 0;
}

module_init(tensorflow_lite_kernel_interpreter_init);
module_exit(tensorflow_lite_kernel_interpreter_exit);
