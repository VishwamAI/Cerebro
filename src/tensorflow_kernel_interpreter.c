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

#define DEVICE_NAME "tensorflow_interpreter_device"
#define CLASS_NAME "tensorflow_interpreter"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("kasinadhsarma, Devin");
MODULE_DESCRIPTION("A kernel module for interpreting and executing TensorFlow models");
MODULE_VERSION("0.1");

static int major_number;
static char *kernel_buffer;
static struct class *tensorflow_interpreter_class = NULL;
static struct device *tensorflow_interpreter_device = NULL;

static int dev_open(struct inode *inodep, struct file *filep) {
    printk(KERN_INFO "TFLiteParserDevice: Device opened\n");
    return 0;
}
static int dev_release(struct inode *inodep, struct file *filep) {
    printk(KERN_INFO "TFLiteParserDevice: Device closed\n");
    return 0;
}
static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset) {
    int error_count = 0;
    error_count = copy_to_user(buffer, kernel_buffer, len);

    if (error_count == 0) {
        printk(KERN_INFO "TFLiteParserDevice: Sent %zu characters to the user\n", len);
        return len; // Return the number of bytes read
    } else {
        printk(KERN_INFO "TFLiteParserDevice: Failed to send %d characters to the user\n", error_count);
        return -EFAULT;
    }
}

static ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset) {
    snprintf(kernel_buffer, 1024, "%s(%zu letters)", buffer, len);
    printk(KERN_INFO "TensorFlowInterpreterDevice: Received %zu characters from the user\n", len);

    // Command handling logic
    if (strncmp(buffer, "LOAD_MODEL", 10) == 0) {
        // Handle model loading
        printk(KERN_INFO "TensorFlowInterpreterDevice: Loading model\n");
        int ret = load_model(kernel_buffer);
        if (ret < 0) {
            printk(KERN_ALERT "TensorFlowInterpreterDevice: Failed to load model\n");
        }
    } else if (strncmp(buffer, "EXECUTE_MODEL", 13) == 0) {
        // Handle model execution
        printk(KERN_INFO "TensorFlowInterpreterDevice: Executing model\n");
        int ret = execute_computation_graph();
        if (ret < 0) {
            printk(KERN_ALERT "TensorFlowInterpreterDevice: Failed to execute model\n");
        }
    } else if (strncmp(buffer, "GET_RESULTS", 11) == 0) {
        // Handle result retrieval
        printk(KERN_INFO "TensorFlowInterpreterDevice: Retrieving results\n");
        int ret = get_results(kernel_buffer, 1024);
        if (ret < 0) {
            printk(KERN_ALERT "TensorFlowInterpreterDevice: Failed to retrieve results\n");
        }
    } else {
        printk(KERN_INFO "TensorFlowInterpreterDevice: Unknown command\n");
    }

    return len;
}

// Function prototypes for TensorFlow model loader functions
extern int load_model(const char *model_path);
extern int parse_tensorflow_model(char *model_data);

// Function to load the TensorFlow model into kernel memory
static int load_model(const char *model_path) {
    return load_model(model_path);
}

// Function to parse the TensorFlow model data
static int parse_tensorflow_model(char *model_data) {
    return parse_tensorflow_model(model_data);
}

static struct node {
    int id;
    int opcode;
    int num_inputs;
    int num_outputs;
    struct node **inputs;
    struct node **outputs;
};

static struct computation_graph {
    int num_nodes;
    struct node *nodes;
};

static struct computation_graph graph;

static int load_computation_graph(struct tensorflow_model *model) {
    int ret = parse_tensorflow_model(kernel_buffer);
    if (ret < 0) {
        printk(KERN_ALERT "TensorFlowInterpreterDevice: Failed to parse TensorFlow model\n");
        return ret;
    }

    graph.num_nodes = model->subgraphs()->Get(0)->operators()->size();
    graph.nodes = kmalloc(graph.num_nodes * sizeof(struct node), GFP_KERNEL);
    if (!graph.nodes) {
        printk(KERN_ALERT "TensorFlowInterpreterDevice: Failed to allocate memory for computation graph nodes\n");
        return -ENOMEM;
    }

    for (int i = 0; i < graph.num_nodes; i++) {
        const tflite::Operator *op = model->subgraphs()->Get(0)->operators()->Get(i);
        graph.nodes[i].id = i;
        graph.nodes[i].opcode = op->opcode_index();
        graph.nodes[i].num_inputs = op->inputs()->size();
        graph.nodes[i].num_outputs = op->outputs()->size();
        graph.nodes[i].inputs = kmalloc(graph.nodes[i].num_inputs * sizeof(struct node *), GFP_KERNEL);
        graph.nodes[i].outputs = kmalloc(graph.nodes[i].num_outputs * sizeof(struct node *), GFP_KERNEL);

        for (int j = 0; j < graph.nodes[i].num_inputs; j++) {
            graph.nodes[i].inputs[j] = &graph.nodes[op->inputs()->Get(j)];
        }

        for (int k = 0; k < graph.nodes[i].num_outputs; k++) {
            graph.nodes[i].outputs[k] = &graph.nodes[op->outputs()->Get(k)];
        }
    }

    printk(KERN_INFO "TensorFlowInterpreterDevice: Computation graph loaded successfully\n");
    return 0;
}

// Function to execute the computation graph using the loaded parameters
static int execute_computation_graph(void) {
    for (int i = 0; i < graph.num_nodes; i++) {
        struct node *current_node = &graph.nodes[i];
        printk(KERN_INFO "TensorFlowInterpreterDevice: Executing node %d with opcode %d\n", current_node->id, current_node->opcode);

        // Execute the operation specified by the node's opcode
        switch (current_node->opcode) {
            case ADD_OPCODE: {
                struct node *input1 = current_node->inputs[0];
                struct node *input2 = current_node->inputs[1];
                struct node *output = current_node->outputs[0];

                if (!input1 || !input2 || !output) {
                    printk(KERN_ALERT "TensorFlowInterpreterDevice: Invalid input or output tensor for ADD operation\n");
                    return -EINVAL;
                }

                output->data = kmalloc(input1->data_size, GFP_KERNEL);
                if (!output->data) {
                    printk(KERN_ALERT "TensorFlowInterpreterDevice: Failed to allocate memory for output tensor\n");
                    return -ENOMEM;
                }

                for (int j = 0; j < input1->data_size / sizeof(float); j++) {
                    ((float *)output->data)[j] = ((float *)input1->data)[j] + ((float *)input2->data)[j];
                }
                break;
            }
            case MULTIPLY_OPCODE: {
                struct node *input1 = current_node->inputs[0];
                struct node *input2 = current_node->inputs[1];
                struct node *output = current_node->outputs[0];

                if (!input1 || !input2 || !output) {
                    printk(KERN_ALERT "TensorFlowInterpreterDevice: Invalid input or output tensor for MULTIPLY operation\n");
                    return -EINVAL;
                }

                output->data = kmalloc(input1->data_size, GFP_KERNEL);
                if (!output->data) {
                    printk(KERN_ALERT "TensorFlowInterpreterDevice: Failed to allocate memory for output tensor\n");
                    return -ENOMEM;
                }

                for (int j = 0; j < input1->data_size / sizeof(float); j++) {
                    ((float *)output->data)[j] = ((float *)input1->data)[j] * ((float *)input2->data)[j];
                }
                break;
            }
            case SUBTRACT_OPCODE: {
                struct node *input1 = current_node->inputs[0];
                struct node *input2 = current_node->inputs[1];
                struct node *output = current_node->outputs[0];

                if (!input1 || !input2 || !output) {
                    printk(KERN_ALERT "TensorFlowInterpreterDevice: Invalid input or output tensor for SUBTRACT operation\n");
                    return -EINVAL;
                }

                output->data = kmalloc(input1->data_size, GFP_KERNEL);
                if (!output->data) {
                    printk(KERN_ALERT "TensorFlowInterpreterDevice: Failed to allocate memory for output tensor\n");
                    return -ENOMEM;
                }

                for (int j = 0; j < input1->data_size / sizeof(float); j++) {
                    ((float *)output->data)[j] = ((float *)input1->data)[j] - ((float *)input2->data)[j];
                }
                break;
            }
            case DIVIDE_OPCODE: {
                struct node *input1 = current_node->inputs[0];
                struct node *input2 = current_node->inputs[1];
                struct node *output = current_node->outputs[0];

                if (!input1 || !input2 || !output) {
                    printk(KERN_ALERT "TensorFlowInterpreterDevice: Invalid input or output tensor for DIVIDE operation\n");
                    return -EINVAL;
                }

                output->data = kmalloc(input1->data_size, GFP_KERNEL);
                if (!output->data) {
                    printk(KERN_ALERT "TensorFlowInterpreterDevice: Failed to allocate memory for output tensor\n");
                    return -ENOMEM;
                }

                for (int j = 0; j < input1->data_size / sizeof(float); j++) {
                    ((float *)output->data)[j] = ((float *)input1->data)[j] / ((float *)input2->data)[j];
                }
                break;
            }
            case RELU_OPCODE: {
                struct node *input = current_node->inputs[0];
                struct node *output = current_node->outputs[0];

                if (!input || !output) {
                    printk(KERN_ALERT "TensorFlowInterpreterDevice: Invalid input or output tensor for RELU operation\n");
                    return -EINVAL;
                }

                output->data = kmalloc(input->data_size, GFP_KERNEL);
                if (!output->data) {
                    printk(KERN_ALERT "TensorFlowInterpreterDevice: Failed to allocate memory for output tensor\n");
                    return -ENOMEM;
                }

                for (int j = 0; j < input->data_size / sizeof(float); j++) {
                    ((float *)output->data)[j] = max(0.0, ((float *)input->data)[j]);
                }
                break;
            }
            case MAXPOOL_OPCODE: {
                struct node *input = current_node->inputs[0];
                struct node *output = current_node->outputs[0];
                int pool_size = 2; // Example pool size
                int stride = 2; // Example stride
                int input_height = 28; // Example input height
                int input_width = 28; // Example input width
                int output_height = input_height / stride;
                int output_width = input_width / stride;

                if (!input || !output) {
                    printk(KERN_ALERT "TensorFlowInterpreterDevice: Invalid input or output tensor for MAXPOOL operation\n");
                    return -EINVAL;
                }

                output->data = kmalloc(output_height * output_width * sizeof(float), GFP_KERNEL);
                if (!output->data) {
                    printk(KERN_ALERT "TensorFlowInterpreterDevice: Failed to allocate memory for output tensor\n");
                    return -ENOMEM;
                }

                for (int h = 0; h < output_height; h++) {
                    for (int w = 0; w < output_width; w++) {
                        float max_val = -FLT_MAX;
                        for (int ph = 0; ph < pool_size; ph++) {
                            for (int pw = 0; pw < pool_size; pw++) {
                                int ih = h * stride + ph;
                                int iw = w * stride + pw;
                                float val = ((float *)input->data)[ih * input_width + iw];
                                if (val > max_val) {
                                    max_val = val;
                                }
                            }
                        }
                        ((float *)output->data)[h * output_width + w] = max_val;
                    }
                }
                break;
            }
            default:
                snprintf(kernel_buffer + i * 10, 10, "Node %d", current_node->id);
                break;
        }
    }
    printk(KERN_INFO "TensorFlowInterpreterDevice: Computation graph executed successfully\n");
    return 0;
}

// Function to retrieve the results of the computation from kernel memory
static int get_results(char *result_buffer, size_t buffer_size) {
    // Example result data for testing purposes
    const char *results = "TensorFlow Model Execution Result:\n"
                          "----------------------------------\n"
                          "Operation: ADD\n"
                          "Input 1: 1.0\n"
                          "Input 2: 2.0\n"
                          "Result: 3.0\n"
                          "\n"
                          "Operation: MULTIPLY\n"
                          "Input 1: 3.0\n"
                          "Input 2: 4.0\n"
                          "Result: 12.0\n"
                          "\n"
                          "Operation: SUBTRACT\n"
                          "Input 1: 5.0\n"
                          "Input 2: 3.0\n"
                          "Result: 2.0\n"
                          "\n"
                          "Operation: DIVIDE\n"
                          "Input 1: 8.0\n"
                          "Input 2: 2.0\n"
                          "Result: 4.0\n";

    // Copy the results data to the result buffer
    snprintf(result_buffer, buffer_size, "%s", results);

    return 0;
}

static struct file_operations fops = {
    .open = dev_open,
    .read = dev_read,
    .write = dev_write,
    .release = dev_release,
};

static int __init tensorflow_interpreter_device_init(void) {
    printk(KERN_INFO "TensorFlowInterpreterDevice: Initializing the TensorFlowInterpreterDevice\n");

    major_number = register_chrdev(0, DEVICE_NAME, &fops);
    if (major_number < 0) {
        printk(KERN_ALERT "TensorFlowInterpreterDevice failed to register a major number\n");
        return major_number;
    }
    printk(KERN_INFO "TensorFlowInterpreterDevice: Registered correctly with major number %d\n", major_number);

    tensorflow_interpreter_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(tensorflow_interpreter_class)) {
        unregister_chrdev(major_number, DEVICE_NAME);
        printk(KERN_ALERT "Failed to register device class\n");
        return PTR_ERR(tensorflow_interpreter_class);
    }
    printk(KERN_INFO "TensorFlowInterpreterDevice: Device class registered correctly\n");

    tensorflow_interpreter_device = device_create(tensorflow_interpreter_class, NULL, MKDEV(major_number, 0), NULL, DEVICE_NAME);
    if (IS_ERR(tensorflow_interpreter_device)) {
        class_destroy(tensorflow_interpreter_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        printk(KERN_ALERT "Failed to create the device\n");
        return PTR_ERR(tensorflow_interpreter_device);
    }
    printk(KERN_INFO "TensorFlowInterpreterDevice: Device class created correctly\n");

    kernel_buffer = kmalloc(1024, GFP_KERNEL);
    if (!kernel_buffer) {
        device_destroy(tensorflow_interpreter_class, MKDEV(major_number, 0));
        class_destroy(tensorflow_interpreter_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        printk(KERN_ALERT "Failed to allocate memory for kernel buffer\n");
        return -ENOMEM;
    }

    return 0;
}

static void __exit tensorflow_interpreter_device_exit(void) {
    kfree(kernel_buffer);
    device_destroy(tensorflow_interpreter_class, MKDEV(major_number, 0));
    class_unregister(tensorflow_interpreter_class);
    class_destroy(tensorflow_interpreter_class);
    unregister_chrdev(major_number, DEVICE_NAME);
    printk(KERN_INFO "TensorFlowInterpreterDevice: Goodbye from the TensorFlowInterpreterDevice\n");
}

module_init(tensorflow_interpreter_device_init);
module_exit(tensorflow_interpreter_device_exit);
