#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/sysinfo.h>
#include <linux/mm.h>

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

static int load_model(const char *model_path);
static int execute_model(void);
static int get_results(char *result_buffer, size_t buffer_size);
static int interpret_and_execute_model(char *model_data);
static struct tensorflow_lite_model *parse_tensorflow_lite_model(char *model_data);
static int load_computation_graph(struct tensorflow_lite_model *model);
static int execute_computation_graph(struct tensorflow_lite_model *model);

static int major_number;
static char *kernel_buffer;

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
    return 0;
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

static ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset) {
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Entering dev_write function\n");
    char model_path[128];
    int ret;
    char *result_buffer = NULL;
    char *temp_buffer = NULL;
    int error_count;
    int snprintf_ret;
    static DEFINE_MUTEX(kernel_buffer_mutex);

    if (!buffer) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Null buffer received\n");
        return -EINVAL;
    }

    if (len > 1023) { // Adjusted to leave space for null terminator
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Buffer length exceeds limit\n");
        return -EINVAL;
    }

    if (len < 1) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Buffer length is too small\n");
        return -EINVAL;
    }

    if (len < 1024 && buffer[len - 1] != '\0') { // Adjusted null-termination check
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Buffer is not null-terminated\n");
        return -EINVAL;
    }

    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Before allocating memory for result buffer\n");
    result_buffer = kmalloc(len + 1, GFP_KERNEL); // Dynamically allocate memory based on actual size
    if (!result_buffer) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Failed to allocate memory for result buffer\n");
        return -ENOMEM;
    }
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Allocated memory for result buffer at %p, size: %zu\n", result_buffer, len + 1);

    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Before allocating memory for temp buffer\n");
    temp_buffer = kmalloc(len + 1, GFP_KERNEL); // Dynamically allocate memory based on actual size
    if (!temp_buffer) {
        kfree(result_buffer);
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Failed to allocate memory for temp buffer\n");
        return -ENOMEM;
    }
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Allocated memory for temp buffer at %p, size: %zu\n", temp_buffer, len + 1);

    if (!kernel_buffer) {
        kfree(result_buffer);
        kfree(temp_buffer);
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Kernel buffer not allocated\n");
        return -ENOMEM;
    }

    if (!mutex_trylock(&kernel_buffer_mutex)) {
        kfree(result_buffer);
        kfree(temp_buffer);
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Failed to acquire mutex lock\n");
        return -EBUSY;
    }
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Mutex locked\n");

    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Before snprintf\n");
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: buffer: %s, len: %zu\n", buffer, len);
    snprintf_ret = snprintf(kernel_buffer, 1023, "%.*s(%zu letters)", (int)(len), buffer, len);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: After snprintf, snprintf_ret: %d, kernel_buffer: %s, kernel_buffer size: %zu\n", snprintf_ret, kernel_buffer, sizeof(kernel_buffer));
    if (snprintf_ret < 0) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: snprintf failed\n");
        goto cleanup;
    }
    if (snprintf_ret >= 1023) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: snprintf output was truncated\n");
        goto cleanup;
    }
    kernel_buffer[1023] = '\0'; // Ensure null-termination

    if (len >= 10 && strncmp(buffer, "LOAD_MODEL", 10) == 0) {
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Before sscanf\n");
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: buffer + 11: %s\n", buffer + 11);
        int sscanf_ret = sscanf(buffer + 11, "%127s", model_path);
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: After sscanf, sscanf_ret: %d, model_path: %s\n", sscanf_ret, model_path);
        if (sscanf_ret != 1) {
            printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Failed to parse model path\n");
            goto cleanup;
        }
        ret = load_model(model_path);
        if (ret < 0) {
            printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Failed to load model from %s\n", model_path);
            goto cleanup;
        }
    } else if (len >= 13 && strncmp(buffer, "EXECUTE_MODEL", 13) == 0) {
        ret = execute_model();
        if (ret < 0) {
            printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Failed to execute model\n");
            goto cleanup;
        }
    } else if (len >= 11 && strncmp(buffer, "GET_RESULTS", 11) == 0) {
        ret = get_results(result_buffer, len + 1);
        if (ret < 0) {
            printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Failed to retrieve results\n");
            goto cleanup;
        } else {
            result_buffer[len] = '\0'; // Ensure null-termination
            strncpy(temp_buffer, result_buffer, len);
            temp_buffer[len] = '\0'; // Ensure null-termination

            if (strnlen(temp_buffer, len) + 1 > len) {
                printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: User buffer too small for results\n");
                goto cleanup;
            }

            printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Before copy_to_user\n");
            printk(KERN_INFO "TensorFlowLiteKernelInterpreter: temp_buffer: %s, len: %zu, temp_buffer size: %zu\n", temp_buffer, len, sizeof(temp_buffer));
            error_count = copy_to_user((void *)buffer, temp_buffer, strnlen(temp_buffer, len) + 1); // Ensure null-terminator is copied
            printk(KERN_INFO "TensorFlowLiteKernelInterpreter: After copy_to_user, error_count: %d\n", error_count);
            if (error_count != 0) {
                printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Failed to copy results to user space\n");
                goto cleanup;
            }
        }
    } else {
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Unknown command\n");
    }

cleanup:
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Cleaning up\n");
    if (result_buffer) {
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Freeing result buffer at %p\n", result_buffer);
        kfree(result_buffer);
    }
    if (temp_buffer) {
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Freeing temp buffer at %p\n", temp_buffer);
        kfree(temp_buffer);
    }
    mutex_unlock(&kernel_buffer_mutex);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Exiting dev_write function\n");
    return strnlen(kernel_buffer, 1024);
}

// Function to load the model from the specified path into kernel memory
static int load_model(const char *model_path) {
    struct file *model_file;
    loff_t pos = 0;
    int ret;
    struct sysinfo mem_info;

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
    loff_t file_size = i_size_read(file_inode(model_file));
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

static int execute_model(void) {
    int ret;

    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Entering execute_model function\n");
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: kernel_buffer before execution: %s\n", kernel_buffer);

    // Log system memory usage before executing the model
    struct sysinfo mem_info;
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
    if (!model || !model->graph) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Invalid model or graph\n");
        return -EINVAL;
    }

    // Implement the actual logic for executing the computation graph
    // For simplicity, assume the graph is a sequence of operations that can be executed in order
    // Iterate through the graph and execute each operation
    uint8_t *graph_data = (uint8_t *)model->graph;
    size_t offset = 0;
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

    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Entering get_results function\n");
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: kernel_buffer before retrieving results: %s\n", kernel_buffer);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: result_buffer before retrieving results: %s\n", result_buffer);

    // Log system memory usage before retrieving results
    struct sysinfo mem_info;
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

static void __exit tensorflow_lite_kernel_interpreter_exit(void) {
    if (kernel_buffer) {
        kfree(kernel_buffer);
    }
    unregister_chrdev(major_number, DEVICE_NAME);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Goodbye from the TensorFlowLiteKernelInterpreter\n");
}

module_init(tensorflow_lite_kernel_interpreter_init);
module_exit(tensorflow_lite_kernel_interpreter_exit);
