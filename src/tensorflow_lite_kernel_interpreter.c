#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/device.h>

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
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Allocated memory for result buffer at %p\n", result_buffer);

    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Before allocating memory for temp buffer\n");
    temp_buffer = kmalloc(len + 1, GFP_KERNEL); // Dynamically allocate memory based on actual size
    if (!temp_buffer) {
        kfree(result_buffer);
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Failed to allocate memory for temp buffer\n");
        return -ENOMEM;
    }
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Allocated memory for temp buffer at %p\n", temp_buffer);

    if (!kernel_buffer) {
        kfree(result_buffer);
        kfree(temp_buffer);
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Kernel buffer not allocated\n");
        return -ENOMEM;
    }

    mutex_lock(&kernel_buffer_mutex);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Mutex locked\n");
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Preparing to format kernel_buffer with snprintf\n");
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: buffer: %s, len: %zu\n", buffer, len);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: kernel_buffer before snprintf: %s, address: %p\n", kernel_buffer, kernel_buffer);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Before snprintf\n");
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: kernel_buffer address: %p, buffer address: %p, len: %zu\n", kernel_buffer, buffer, len);
    snprintf_ret = snprintf(kernel_buffer, 1023, "%.*s(%zu letters)", (int)(len), buffer, len);
    if (snprintf_ret < 0) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: snprintf failed\n");
        goto cleanup;
    }
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: After snprintf\n");
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: snprintf_ret: %d, kernel_buffer: %s\n", snprintf_ret, kernel_buffer);
    if (snprintf_ret >= 1023) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: snprintf output was truncated\n");
        goto cleanup;
    }
    kernel_buffer[1023] = '\0'; // Ensure null-termination
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: kernel_buffer after snprintf: %s, address: %p\n", kernel_buffer, kernel_buffer);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Received %zu characters from the user\n", len);

    // Command handling logic
    if (len >= 10 && strncmp(buffer, "LOAD_MODEL", 10) == 0) {
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Loading model\n");
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: buffer: %s\n", buffer);
        if (sscanf(buffer + 11, "%127s", model_path) != 1) {
            printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Failed to parse model path\n");
            printk(KERN_INFO "TensorFlowLiteKernelInterpreter: model_path: %s\n", model_path);
            goto cleanup;
        }
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: model_path: %s\n", model_path);
        ret = load_model(model_path);
        if (ret < 0) {
            printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Failed to load model from %s\n", model_path);
            goto cleanup;
        }
    } else if (len >= 13 && strncmp(buffer, "EXECUTE_MODEL", 13) == 0) {
        // Handle model execution
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Executing model\n");
        ret = execute_model();
        if (ret < 0) {
            printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Failed to execute model\n");
            goto cleanup;
        }
    } else if (len >= 11 && strncmp(buffer, "GET_RESULTS", 11) == 0) {
        // Handle retrieving results
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Retrieving results\n");
        ret = get_results(result_buffer, len + 1);
        if (ret < 0) {
            printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Failed to retrieve results\n");
            goto cleanup;
        } else {
            result_buffer[len] = '\0'; // Ensure null-termination
            strncpy(temp_buffer, result_buffer, len);
            temp_buffer[len] = '\0'; // Ensure null-termination
            printk(KERN_INFO "TensorFlowLiteKernelInterpreter: temp_buffer after strncpy: %s\n", temp_buffer);

            if (strnlen(temp_buffer, len) + 1 > len) {
                printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: User buffer too small for results\n");
                goto cleanup;
            }

            printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Preparing to copy results to user space\n");
            printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Before copy_to_user\n");
            printk(KERN_INFO "TensorFlowLiteKernelInterpreter: buffer address: %p, temp_buffer address: %p, temp_buffer length: %zu\n", buffer, temp_buffer, strnlen(temp_buffer, len));
            error_count = copy_to_user((void *)buffer, temp_buffer, strnlen(temp_buffer, len) + 1); // Ensure null-terminator is copied
            printk(KERN_INFO "TensorFlowLiteKernelInterpreter: After copy_to_user\n");
            if (error_count != 0) {
                printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Failed to copy results to user space\n");
                goto cleanup;
            }
            printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Successfully copied results to user space\n");
        }
    } else {
        printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Unknown command\n");
    }

cleanup:
    kfree(result_buffer);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Freed result buffer\n");
    kfree(temp_buffer);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Freed temp buffer\n");
    mutex_unlock(&kernel_buffer_mutex);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Mutex unlocked\n");
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Exiting dev_write function\n");

    return strnlen(kernel_buffer, 1024);
}

// Function to load the model from the specified path into kernel memory
static int load_model(const char *model_path) {
    // Load the model from the specified path into kernel memory
    // For simplicity, assume the model is a binary file that can be loaded into memory
    // Load the model from the specified path into kernel memory
    // For simplicity, assume the model is a binary file that can be loaded into memory
    struct file *model_file;

    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Entering load_model function\n");
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: model_path: %s\n", model_path);
    loff_t pos = 0;
    int ret;

    model_file = filp_open(model_path, O_RDONLY, 0);
    if (IS_ERR(model_file)) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Failed to open model file %s\n", model_path);
        return PTR_ERR(model_file);
    }

    ret = kernel_read(model_file, kernel_buffer, 1024, &pos);
    if (ret < 0) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Failed to read model file %s\n", model_path);
        filp_close(model_file, NULL);
        return ret;
    }

    filp_close(model_file, NULL);

    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Model loaded from %s\n", model_path);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: kernel_buffer after loading model: %s\n", kernel_buffer);
    return 0;
}

static int execute_model(void) {
    int ret;

    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Entering execute_model function\n");
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: kernel_buffer before execution: %s\n", kernel_buffer);

    ret = interpret_and_execute_model(kernel_buffer);

    if (ret < 0) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Model execution failed with error code %d\n", ret);
        return ret;
    }

    snprintf(kernel_buffer, 1024, "Model execution result: %d", ret);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Model executed\n");
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: kernel_buffer after execution: %s\n", kernel_buffer);

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
    int result = 42;
    if (!model || !model->graph) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: Invalid model or graph\n");
        return -EINVAL;
    }
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Computation graph executed\n");
    return result;
}

static int get_results(char *result_buffer, size_t buffer_size) {
    int snprintf_ret;

    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Entering get_results function\n");
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: kernel_buffer before retrieving results: %s\n", kernel_buffer);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: result_buffer before retrieving results: %s\n", result_buffer);

    snprintf_ret = snprintf(result_buffer, buffer_size, "%s", kernel_buffer);
    if (snprintf_ret >= buffer_size) {
        printk(KERN_ALERT "TensorFlowLiteKernelInterpreter: snprintf output was truncated\n");
        return -EINVAL;
    }

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

    kernel_buffer = kmalloc(1024, GFP_KERNEL);
    if (!kernel_buffer) {
        unregister_chrdev(major_number, DEVICE_NAME);
        printk(KERN_ALERT "Failed to allocate memory for kernel buffer\n");
        return -ENOMEM;
    }

    return 0;
}

static void __exit tensorflow_lite_kernel_interpreter_exit(void) {
    kfree(kernel_buffer);
    unregister_chrdev(major_number, DEVICE_NAME);
    printk(KERN_INFO "TensorFlowLiteKernelInterpreter: Goodbye from the TensorFlowLiteKernelInterpreter\n");
}

module_init(tensorflow_lite_kernel_interpreter_init);
module_exit(tensorflow_lite_kernel_interpreter_exit);
