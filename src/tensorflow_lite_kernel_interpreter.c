#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/sysinfo.h>
#include <linux/mm.h>
#include <linux/device.h>
#include "model_interpreter.h"
#include <linux/delay.h>

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
    mutex_unlock(&kernel_buffer_mutex);
    return 0;
}

/* Removed redundant definition of tensorflow_lite_kernel_interpreter_exit */

static ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset) {
    int ret;
    int snprintf_ret;
    char *result_buffer = NULL;
    int error_count;
    struct sysinfo mem_info;
    int retry_count;

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
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: User space buffer is accessible and passed access_ok check\n");

    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Attempting to lock kernel_buffer_mutex\n");
    mutex_lock(&kernel_buffer_mutex);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Successfully locked kernel_buffer_mutex\n");

    // Log system memory usage before allocating the kernel buffer
    si_meminfo(&mem_info);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Memory before allocating kernel buffer - Total: %lu, Free: %lu, Available: %lu\n",
           mem_info.totalram, mem_info.freeram, mem_info.freeram + mem_info.bufferram);

    // Log the context in which vmalloc is being called
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: in_atomic() = %d\n", in_atomic());

    if (kernel_buffer) {
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Freeing existing kernel buffer at %p\n", kernel_buffer);
        vfree(kernel_buffer);
        kernel_buffer = NULL;
    }

    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Attempting to allocate kernel buffer with size %zu\n", len + 1);
    if (in_atomic()) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Cannot allocate memory in atomic context\n");
        mutex_unlock(&kernel_buffer_mutex);
        return -EFAULT;
    }

    retry_count = 5;
    while (retry_count > 0) {
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Attempting vmalloc, retry_count=%d\n", retry_count);
        kernel_buffer = vmalloc(len + 1);
        if (kernel_buffer) {
            printk(KERN_INFO "TensorFlowLiteKernelInterpreter: vmalloc succeeded, kernel_buffer=%p\n", kernel_buffer);
            break;
        } else {
            printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: vmalloc failed, kernel_buffer is NULL\n");
        }
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: vmalloc failed, retrying... (%d retries left)\n", retry_count - 1);
        si_meminfo(&mem_info);
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Memory after vmalloc failure - Total: %lu, Free: %lu, Available: %lu\n",
               mem_info.totalram, mem_info.freeram, mem_info.freeram + mem_info.bufferram);
        msleep(200);
        retry_count--;
    }

    if (!kernel_buffer) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Failed to allocate memory for kernel buffer with vmalloc, attempting kmalloc\n");

        si_meminfo(&mem_info);
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Memory immediately after vmalloc failure - Total: %lu, Free: %lu, Available: %lu\n",
               mem_info.totalram, mem_info.freeram, mem_info.freeram + mem_info.bufferram);

        retry_count = 3;
        while (retry_count > 0) {
            printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Attempting kmalloc, retry_count=%d\n", retry_count);
            kernel_buffer = kmalloc(len + 1, GFP_KERNEL);
            if (kernel_buffer) {
                printk(KERN_INFO "TensorFlowLiteKernelInterpreter: kmalloc succeeded, kernel_buffer=%p\n", kernel_buffer);
                break;
            } else {
                printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: kmalloc failed, kernel_buffer is NULL\n");
            }
            printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: kmalloc failed, retrying... (%d retries left)\n", retry_count - 1);
            si_meminfo(&mem_info);
            printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Memory after kmalloc failure - Total: %lu, Free: %lu, Available: %lu\n",
                   mem_info.totalram, mem_info.freeram, mem_info.freeram + mem_info.bufferram);
            msleep(100);
            retry_count--;
        }

        if (!kernel_buffer) {
            printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Failed to allocate memory for kernel buffer with kmalloc\n");
            mutex_unlock(&kernel_buffer_mutex);
            return -ENOMEM;
        }
    }

    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Successfully allocated kernel buffer with size %zu\n", len + 1);

    // Log system memory usage after successful allocation
    si_meminfo(&mem_info);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Memory after allocation - Total: %lu, Free: %lu, Available: %lu\n",
           mem_info.totalram, mem_info.freeram, mem_info.freeram + mem_info.bufferram);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Successfully allocated kernel buffer with size %zu\n", len + 1);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Kernel buffer state after allocation: %s\n", kernel_buffer ? "Allocated" : "Not Allocated");

    if (!kernel_buffer) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Kernel buffer is not allocated\n");
        mutex_unlock(&kernel_buffer_mutex);
        return -ENOMEM;
    }

    // Additional logging to verify kernel buffer integrity before processing commands
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Kernel buffer before processing commands: %s\n", kernel_buffer);

    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Before memcpy, buffer: %s\n", buffer);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Before memcpy, kernel_buffer: %p\n", kernel_buffer);
    if (!kernel_buffer) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Kernel buffer is not allocated\n");
        mutex_unlock(&kernel_buffer_mutex);
        return -ENOMEM;
    }
    if (copy_from_user(kernel_buffer, buffer, len)) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Failed to copy data from user space\n");
        vfree(kernel_buffer);
        kernel_buffer = NULL;
        mutex_unlock(&kernel_buffer_mutex);
        return -EFAULT;
    }
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: After memcpy, kernel_buffer: %s\n", kernel_buffer);
    kernel_buffer[len] = '\0'; // Ensure null-termination
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Kernel buffer state after null-termination: %s\n", kernel_buffer);

    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Parsing command from kernel buffer\n");
    // Parse the command from the kernel buffer
    if (strncmp(kernel_buffer, "LOAD_MODEL ", 11) == 0) {
        char *model_path = kernel_buffer + 11;
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Loading model from path: %s\n", model_path);
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: model_path before calling load_model: %s\n", model_path);
        ret = load_model(model_path);
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: load_model function returned %d\n", ret);
        if (ret < 0) {
            printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Failed to load model from path: %s\n", model_path);
            vfree(kernel_buffer);
            kernel_buffer = NULL;
            mutex_unlock(&kernel_buffer_mutex);
            return ret;
        }
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Model loaded successfully\n");
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: kernel_buffer after loading model: %s\n", kernel_buffer);
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Exiting LOAD_MODEL command handling\n");
    } else if (strncmp(kernel_buffer, "EXECUTE_MODEL", 13) == 0) {
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Executing model\n");
        ret = execute_model();
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: execute_model function returned %d\n", ret);
        if (ret < 0) {
            printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Model execution failed\n");
            vfree(kernel_buffer);
            kernel_buffer = NULL;
            mutex_unlock(&kernel_buffer_mutex);
            return ret;
        }
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Model executed successfully\n");
    } else if (strncmp(kernel_buffer, "GET_RESULTS", 11) == 0) {
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Getting results\n");
        result_buffer = kmalloc(2048, GFP_KERNEL); // Allocate result buffer with sufficient size
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: result_buffer allocated at %p\n", result_buffer);
        if (!result_buffer) {
            printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Failed to allocate memory for result buffer\n");
            vfree(kernel_buffer);
            kernel_buffer = NULL;
            mutex_unlock(&kernel_buffer_mutex);
            return -ENOMEM;
        }

        ret = get_results(result_buffer, 2048);
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: get_results function returned %d\n", ret);
        if (ret < 0) {
            printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Failed to retrieve results\n");
            kfree(result_buffer);
            vfree(kernel_buffer);
            kernel_buffer = NULL;
            mutex_unlock(&kernel_buffer_mutex);
            return ret;
        } else {
            result_buffer[2047] = '\0'; // Ensure null-termination
            printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Before copy_to_user, result_buffer: %s\n", result_buffer);
            error_count = copy_to_user((void *)buffer, result_buffer, strnlen(result_buffer, 2048) + 1); // Ensure null-terminator is copied
            printk(KERN_INFO "TensorFlowLiteKernelInterpreter: After copy_to_user, error_count: %d\n", error_count);
            if (error_count != 0) {
                printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Failed to copy results to user space\n");
                kfree(result_buffer);
                vfree(kernel_buffer);
                kernel_buffer = NULL;
                mutex_unlock(&kernel_buffer_mutex);
                return -EFAULT;
            }
        }
        kfree(result_buffer);
    } else {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Unknown command\n");
        vfree(kernel_buffer);
        kernel_buffer = NULL;
        mutex_unlock(&kernel_buffer_mutex);
        return -EINVAL;
    }

    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: After snprintf, kernel_buffer: %s\n", kernel_buffer);
    if (snprintf_ret < 0) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: snprintf failed with error code %d\n", snprintf_ret);
        vfree(kernel_buffer);
        kernel_buffer = NULL;
        mutex_unlock(&kernel_buffer_mutex);
        return snprintf_ret;
    } else if (snprintf_ret >= 1024) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: snprintf output was truncated\n");
        vfree(kernel_buffer);
        kernel_buffer = NULL;
        mutex_unlock(&kernel_buffer_mutex);
        return -EINVAL;
    }
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: snprintf completed successfully, kernel_buffer: %s\n", kernel_buffer);

    vfree(kernel_buffer);
    kernel_buffer = NULL;
    mutex_unlock(&kernel_buffer_mutex);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Exiting dev_write function\n");
    return len;
}

int load_model(const char *model_path) {
    struct file *model_file;
    loff_t pos = 0;
    ssize_t ret;
    struct sysinfo mem_info;
    loff_t file_size;

    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Entering load_model function\n");
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: model_path: %s\n", model_path);

    // Log system memory usage before loading the model
    si_meminfo(&mem_info);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Memory before loading model - Total: %lu, Free: %lu, Available: %lu\n",
           mem_info.totalram, mem_info.freeram, mem_info.freeram + mem_info.bufferram);

    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Attempting to open model file %s\n", model_path);

    // Ensure the model_path is null-terminated and within the maximum allowed length
    if (strnlen(model_path, PATH_MAX) == PATH_MAX) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Model path exceeds maximum allowed length\n");
        return -ENAMETOOLONG;
    }

    model_file = filp_open(model_path, O_RDONLY, 0);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: filp_open returned %p\n", model_file);
    if (IS_ERR(model_file)) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Failed to open model file %s with error %ld\n", model_path, PTR_ERR(model_file));
        return PTR_ERR(model_file);
    }
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Successfully opened model file %s\n", model_path);

    // Determine the size of the model file
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Attempting to read model file size\n");
    file_size = i_size_read(file_inode(model_file));
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Model file size: %lld\n", file_size);

    // Free existing kernel_buffer if already allocated
    if (kernel_buffer) {
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Freeing existing kernel buffer at %p\n", kernel_buffer);
        vfree(kernel_buffer);
        kernel_buffer = NULL;
    }

    // Ensure kernel_buffer is allocated
    if (!kernel_buffer) {
        kernel_buffer = vmalloc(file_size + 1);
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Allocated kernel buffer at %p\n", kernel_buffer);
        if (!kernel_buffer) {
            printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Failed to allocate memory for kernel buffer\n");
            filp_close(model_file, NULL);
            return -ENOMEM;
        }
    } else {
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Kernel buffer is already allocated\n");
    }

    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Attempting to read model file into kernel buffer\n");
    // Read the model file into kernel_buffer
    ret = kernel_read(model_file, kernel_buffer, file_size, &pos);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: kernel_read returned %ld\n", ret);
    if (ret < 0) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Failed to read model file %s with error %ld\n", model_path, ret);
        vfree(kernel_buffer);
        filp_close(model_file, NULL);
        return ret;
    }
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Successfully read model file into kernel buffer\n");
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: kernel_buffer after reading model file: %s\n", kernel_buffer);

    filp_close(model_file, NULL);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Closed model file %s\n", model_path);

    // Log system memory usage after loading the model
    si_meminfo(&mem_info);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Memory after loading model - Total: %lu, Free: %lu, Available: %lu\n",
           mem_info.totalram, mem_info.freeram, mem_info.freeram + mem_info.bufferram);

    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Model loaded from %s\n", model_path);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: kernel_buffer after loading model: %s\n", kernel_buffer);
    return 0;
}

/* Removed redundant definition of tensorflow_lite_kernel_interpreter_exit */
/* Removed redundant definition of tensorflow_lite_kernel_interpreter_exit */

/* Removed redundant definition of tensorflow_lite_kernel_interpreter_exit */

/* Removed redundant definition of tensorflow_lite_kernel_interpreter_exit */

/* Removed redundant definition of tensorflow_lite_kernel_interpreter_exit */

/* Removed redundant definition of tensorflow_lite_kernel_interpreter_exit */

/* Removed redundant definition of tensorflow_lite_kernel_interpreter_exit */

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

int execute_model(void) {
    int ret;
    int snprintf_ret;
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

    snprintf_ret = snprintf(kernel_buffer, 1024, "Model execution result: %d", ret);
    if (snprintf_ret >= 1024) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: snprintf output was truncated\n");
        vfree(kernel_buffer);
        kernel_buffer = NULL;
        mutex_unlock(&kernel_buffer_mutex);
        return -EINVAL;
    }
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: snprintf completed, kernel_buffer: %s\n", kernel_buffer);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: snprintf returned %d\n", snprintf_ret);
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

    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Entering interpret_and_execute_model function\n");
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: model_data: %s\n", model_data);

    if (!model) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Failed to parse model data\n");
        return -EINVAL;
    }
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Successfully parsed model data\n");

    ret = load_computation_graph(model);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: load_computation_graph function returned %d\n", ret);
    if (ret < 0) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Failed to load computation graph\n");
        kfree(model);
        return ret;
    }
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Computation graph loaded successfully\n");

    result = execute_computation_graph(model);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: execute_computation_graph function returned %d\n", result);

    kfree(model->graph);
    kfree(model);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Freed model and graph memory\n");

    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Exiting interpret_and_execute_model function\n");
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

    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: result_buffer before snprintf: %s\n", result_buffer);

    snprintf_ret = snprintf(result_buffer, buffer_size, "%s", kernel_buffer);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: snprintf completed, result_buffer: %s\n", result_buffer);
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

static struct class* tensorflow_lite_class = NULL;
static struct device* tensorflow_lite_device = NULL;

static int __init tensorflow_lite_kernel_interpreter_init(void) {
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Initializing the TensorFlowLiteKernelInterpreter\n");

    major_number = register_chrdev(0, DEVICE_NAME, &fops);
    if (major_number < 0) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter failed to register a major number\n");
        return major_number;
    }
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Registered correctly with major number %d\n", major_number);

    // Register the device class
    tensorflow_lite_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(tensorflow_lite_class)) {
        unregister_chrdev(major_number, DEVICE_NAME);
        printk(KERN_ALERT "Failed to register device class\n");
        return PTR_ERR(tensorflow_lite_class);
    }
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Device class registered correctly\n");

    // Register the device driver
    tensorflow_lite_device = device_create(tensorflow_lite_class, NULL, MKDEV(major_number, 0), NULL, DEVICE_NAME);
    if (IS_ERR(tensorflow_lite_device)) {
        class_destroy(tensorflow_lite_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        printk(KERN_ALERT "Failed to create the device\n");
        return PTR_ERR(tensorflow_lite_device);
    }
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Device class created correctly\n");

    return 0;
}

static void __exit tensorflow_lite_kernel_interpreter_exit(void) {
    device_destroy(tensorflow_lite_class, MKDEV(major_number, 0));
    class_unregister(tensorflow_lite_class);
    class_destroy(tensorflow_lite_class);
    unregister_chrdev(major_number, DEVICE_NAME);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Goodbye from the TensorFlowLiteKernelInterpreter\n");
}

module_init(tensorflow_lite_kernel_interpreter_init);
module_exit(tensorflow_lite_kernel_interpreter_exit);
