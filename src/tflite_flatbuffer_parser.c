#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <flatbuffers/flatbuffers.h>
#include "schema_v3c_generated.h"

#define DEVICE_NAME "tflite_parser_device"
#define CLASS_NAME "tflite_parser"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("kasinadhsarma, Devin");
MODULE_DESCRIPTION("A kernel module for parsing and executing TensorFlow Lite models");
MODULE_VERSION("0.1");

static int major_number;
static char *kernel_buffer;
static struct class *tflite_parser_class = NULL;
static struct device *tflite_parser_device = NULL;

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
        return 0;
    } else {
        printk(KERN_INFO "TFLiteParserDevice: Failed to send %d characters to the user\n", error_count);
        return -EFAULT;
    }
}

static ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset) {
    snprintf(kernel_buffer, 1024, "%s(%zu letters)", buffer, len);
    printk(KERN_INFO "TFLiteParserDevice: Received %zu characters from the user\n", len);

    // Command handling logic
    if (strncmp(buffer, "PARSE_MODEL", 11) == 0) {
        // Handle model parsing
        printk(KERN_INFO "TFLiteParserDevice: Parsing model\n");
        int ret = parse_model(kernel_buffer);
        if (ret < 0) {
            printk(KERN_ALERT "TFLiteParserDevice: Failed to parse model\n");
        }
    } else {
        printk(KERN_INFO "TFLiteParserDevice: Unknown command\n");
    }

    return len;
}

// Function to parse the TensorFlow Lite model from the kernel buffer
static int parse_model(const char *model_data) {
    flatbuffers::Verifier verifier((const uint8_t *)model_data, 1024);
    if (!tflite::VerifyModelBuffer(verifier)) {
        printk(KERN_ALERT "TFLiteParserDevice: Invalid model buffer\n");
        return -EINVAL;
    }

    const tflite::Model *model = tflite::GetModel(model_data);
    if (model == NULL) {
        printk(KERN_ALERT "TFLiteParserDevice: Failed to get model from buffer\n");
        return -EINVAL;
    }

    printk(KERN_INFO "TFLiteParserDevice: Model has %d subgraphs\n", model->subgraphs()->size());

    // Iterate over subgraphs
    for (auto subgraph : *model->subgraphs()) {
        if (subgraph == NULL) {
            printk(KERN_ALERT "TFLiteParserDevice: Subgraph is NULL\n");
            return -EINVAL;
        }
        printk(KERN_INFO "TFLiteParserDevice: Subgraph has %d tensors\n", subgraph->tensors()->size());

        // Iterate over tensors
        for (auto tensor : *subgraph->tensors()) {
            if (tensor == NULL) {
                printk(KERN_ALERT "TFLiteParserDevice: Tensor is NULL\n");
                return -EINVAL;
            }
            printk(KERN_INFO "TFLiteParserDevice: Tensor type: %d\n", tensor->type());
            printk(KERN_INFO "TFLiteParserDevice: Tensor shape: ");
            for (auto dim : *tensor->shape()) {
                printk(KERN_CONT "%d ", dim);
            }
            printk(KERN_CONT "\n");

            // Handle tensor data buffer
            const tflite::Buffer *buffer = model->buffers()->Get(tensor->buffer());
            if (buffer == NULL || buffer->data() == NULL) {
                printk(KERN_INFO "TFLiteParserDevice: Tensor has no data buffer\n");
            } else {
                printk(KERN_INFO "TFLiteParserDevice: Tensor data size: %d\n", buffer->data()->size());
            }
        }

        // Iterate over operators
        for (auto op : *subgraph->operators()) {
            if (op == NULL) {
                printk(KERN_ALERT "TFLiteParserDevice: Operator is NULL\n");
                return -EINVAL;
            }
            const tflite::OperatorCode *op_code = model->operator_codes()->Get(op->opcode_index());
            if (op_code == NULL) {
                printk(KERN_ALERT "TFLiteParserDevice: Operator code is NULL\n");
                return -EINVAL;
            }
            printk(KERN_INFO "TFLiteParserDevice: Operator code: %d\n", op_code->builtin_code());

            // Example: Handle ADD operator
            if (op_code->builtin_code() == tflite::BuiltinOperator_ADD) {
                printk(KERN_INFO "TFLiteParserDevice: Handling ADD operator\n");
                // Add logic to execute ADD operator
                execute_add_operator(op, subgraph, model);
            } else if (op_code->builtin_code() == tflite::BuiltinOperator_MUL) {
                printk(KERN_INFO "TFLiteParserDevice: Handling MUL operator\n");
                // Add logic to execute MUL operator
                execute_mul_operator(op, subgraph, model);
            } else if (op_code->builtin_code() == tflite::BuiltinOperator_SUB) {
                printk(KERN_INFO "TFLiteParserDevice: Handling SUB operator\n");
                // Add logic to execute SUB operator
                execute_sub_operator(op, subgraph, model);
            } else {
                printk(KERN_INFO "TFLiteParserDevice: Unsupported operator\n");
            }
        }
    }

    return 0;
}

static void execute_add_operator(const tflite::Operator *op, const tflite::SubGraph *subgraph, const tflite::Model *model) {
    // Get input and output tensor indices
    int input1_idx = op->inputs()->Get(0);
    int input2_idx = op->inputs()->Get(1);
    int output_idx = op->outputs()->Get(0);

    // Get input and output tensors
    const tflite::Tensor *input1 = subgraph->tensors()->Get(input1_idx);
    const tflite::Tensor *input2 = subgraph->tensors()->Get(input2_idx);
    const tflite::Tensor *output = subgraph->tensors()->Get(output_idx);

    // Validate tensors
    if (input1 == NULL || input2 == NULL || output == NULL) {
        printk(KERN_ALERT "TFLiteParserDevice: One or more tensors are NULL in ADD operator\n");
        return;
    }

    // Validate tensor shapes
    if (input1->shape()->Length() != input2->shape()->Length() || input1->shape()->Length() != output->shape()->Length()) {
        printk(KERN_ALERT "TFLiteParserDevice: Tensor shapes are incompatible in ADD operator\n");
        return;
    }

    // Get input and output data buffers
    const tflite::Buffer *input1_buffer = model->buffers()->Get(input1->buffer());
    const tflite::Buffer *input2_buffer = model->buffers()->Get(input2->buffer());
    tflite::Buffer *output_buffer = model->buffers()->Get(output->buffer());

    // Validate buffers
    if (input1_buffer == NULL || input2_buffer == NULL || output_buffer == NULL) {
        printk(KERN_ALERT "TFLiteParserDevice: One or more buffers are NULL in ADD operator\n");
        return;
    }

    // Handle quantization parameters
    float input1_scale = input1->quantization() ? input1->quantization()->scale()->Get(0) : 1.0f;
    int32_t input1_zero_point = input1->quantization() ? input1->quantization()->zero_point()->Get(0) : 0;
    float input2_scale = input2->quantization() ? input2->quantization()->scale()->Get(0) : 1.0f;
    int32_t input2_zero_point = input2->quantization() ? input2->quantization()->zero_point()->Get(0) : 0;
    float output_scale = output->quantization() ? output->quantization()->scale()->Get(0) : 1.0f;
    int32_t output_zero_point = output->quantization() ? output->quantization()->zero_point()->Get(0) : 0;

    // Perform the ADD operation
    for (int i = 0; i < input1->shape()->Length(); i++) {
        int32_t input1_val = input1_buffer->data()->Get(i) - input1_zero_point;
        int32_t input2_val = input2_buffer->data()->Get(i) - input2_zero_point;
        int32_t output_val = input1_val + input2_val;
        output_buffer->mutable_data()->Get(i) = output_val + output_zero_point;
    }

    printk(KERN_INFO "TFLiteParserDevice: ADD operator executed\n");
}

static void execute_mul_operator(const tflite::Operator *op, const tflite::SubGraph *subgraph, const tflite::Model *model) {
    // Get input and output tensor indices
    int input1_idx = op->inputs()->Get(0);
    int input2_idx = op->inputs()->Get(1);
    int output_idx = op->outputs()->Get(0);

    // Get input and output tensors
    const tflite::Tensor *input1 = subgraph->tensors()->Get(input1_idx);
    const tflite::Tensor *input2 = subgraph->tensors()->Get(input2_idx);
    const tflite::Tensor *output = subgraph->tensors()->Get(output_idx);

    // Validate tensors
    if (input1 == NULL || input2 == NULL || output == NULL) {
        printk(KERN_ALERT "TFLiteParserDevice: One or more tensors are NULL in MUL operator\n");
        return;
    }

    // Validate tensor shapes
    if (input1->shape()->Length() != input2->shape()->Length() || input1->shape()->Length() != output->shape()->Length()) {
        printk(KERN_ALERT "TFLiteParserDevice: Tensor shapes are incompatible in MUL operator\n");
        return;
    }

    // Get input and output data buffers
    const tflite::Buffer *input1_buffer = model->buffers()->Get(input1->buffer());
    const tflite::Buffer *input2_buffer = model->buffers()->Get(input2->buffer());
    tflite::Buffer *output_buffer = model->buffers()->Get(output->buffer());

    // Validate buffers
    if (input1_buffer == NULL || input2_buffer == NULL || output_buffer == NULL) {
        printk(KERN_ALERT "TFLiteParserDevice: One or more buffers are NULL in MUL operator\n");
        return;
    }

    // Handle quantization parameters
    float input1_scale = input1->quantization() ? input1->quantization()->scale()->Get(0) : 1.0f;
    int32_t input1_zero_point = input1->quantization() ? input1->quantization()->zero_point()->Get(0) : 0;
    float input2_scale = input2->quantization() ? input2->quantization()->scale()->Get(0) : 1.0f;
    int32_t input2_zero_point = input2->quantization() ? input2->quantization()->zero_point()->Get(0) : 0;
    float output_scale = output->quantization() ? output->quantization()->scale()->Get(0) : 1.0f;
    int32_t output_zero_point = output->quantization() ? output->quantization()->zero_point()->Get(0) : 0;

    // Perform the MUL operation
    for (int i = 0; i < input1->shape()->Length(); i++) {
        int32_t input1_val = input1_buffer->data()->Get(i) - input1_zero_point;
        int32_t input2_val = input2_buffer->data()->Get(i) - input2_zero_point;
        int32_t output_val = input1_val * input2_val;
        output_buffer->mutable_data()->Get(i) = output_val + output_zero_point;
    }

    printk(KERN_INFO "TFLiteParserDevice: MUL operator executed\n");
}

static void execute_sub_operator(const tflite::Operator *op, const tflite::SubGraph *subgraph, const tflite::Model *model) {
    // Get input and output tensor indices
    int input1_idx = op->inputs()->Get(0);
    int input2_idx = op->inputs()->Get(1);
    int output_idx = op->outputs()->Get(0);

    // Get input and output tensors
    const tflite::Tensor *input1 = subgraph->tensors()->Get(input1_idx);
    const tflite::Tensor *input2 = subgraph->tensors()->Get(input2_idx);
    const tflite::Tensor *output = subgraph->tensors()->Get(output_idx);

    // Validate tensors
    if (input1 == NULL || input2 == NULL || output == NULL) {
        printk(KERN_ALERT "TFLiteParserDevice: One or more tensors are NULL in SUB operator\n");
        return;
    }

    // Validate tensor shapes
    if (input1->shape()->Length() != input2->shape()->Length() || input1->shape()->Length() != output->shape()->Length()) {
        printk(KERN_ALERT "TFLiteParserDevice: Tensor shapes are incompatible in SUB operator\n");
        return;
    }

    // Get input and output data buffers
    const tflite::Buffer *input1_buffer = model->buffers()->Get(input1->buffer());
    const tflite::Buffer *input2_buffer = model->buffers()->Get(input2->buffer());
    tflite::Buffer *output_buffer = model->buffers()->Get(output->buffer());

    // Validate buffers
    if (input1_buffer == NULL || input2_buffer == NULL || output_buffer == NULL) {
        printk(KERN_ALERT "TFLiteParserDevice: One or more buffers are NULL in SUB operator\n");
        return;
    }

    // Handle quantization parameters
    float input1_scale = input1->quantization() ? input1->quantization()->scale()->Get(0) : 1.0f;
    int32_t input1_zero_point = input1->quantization() ? input1->quantization()->zero_point()->Get(0) : 0;
    float input2_scale = input2->quantization() ? input2->quantization()->scale()->Get(0) : 1.0f;
    int32_t input2_zero_point = input2->quantization() ? input2->quantization()->zero_point()->Get(0) : 0;
    float output_scale = output->quantization() ? output->quantization()->scale()->Get(0) : 1.0f;
    int32_t output_zero_point = output->quantization() ? output->quantization()->zero_point()->Get(0) : 0;

    // Perform the SUB operation
    for (int i = 0; i < input1->shape()->Length(); i++) {
        int32_t input1_val = input1_buffer->data()->Get(i) - input1_zero_point;
        int32_t input2_val = input2_buffer->data()->Get(i) - input2_zero_point;
        int32_t output_val = input1_val - input2_val;
        output_buffer->mutable_data()->Get(i) = output_val + output_zero_point;
    }

    printk(KERN_INFO "TFLiteParserDevice: SUB operator executed\n");
}

static struct file_operations fops = {
    .open = dev_open,
    .read = dev_read,
    .write = dev_write,
    .release = dev_release,
};
static int __init tflite_parser_device_init(void) {
    printk(KERN_INFO "TFLiteParserDevice: Initializing the TFLiteParserDevice\n");

    major_number = register_chrdev(0, DEVICE_NAME, &fops);
    if (major_number < 0) {
        printk(KERN_ALERT "TFLiteParserDevice failed to register a major number\n");
        return major_number;
    }
    printk(KERN_INFO "TFLiteParserDevice: Registered correctly with major number %d\n", major_number);

    tflite_parser_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(tflite_parser_class)) {
        unregister_chrdev(major_number, DEVICE_NAME);
        printk(KERN_ALERT "Failed to register device class\n");
        return PTR_ERR(tflite_parser_class);
    }
    printk(KERN_INFO "TFLiteParserDevice: Device class registered correctly\n");

    tflite_parser_device = device_create(tflite_parser_class, NULL, MKDEV(major_number, 0), NULL, DEVICE_NAME);
    if (IS_ERR(tflite_parser_device)) {
        class_destroy(tflite_parser_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        printk(KERN_ALERT "Failed to create the device\n");
        return PTR_ERR(tflite_parser_device);
    }
    printk(KERN_INFO "TFLiteParserDevice: Device class created correctly\n");

    kernel_buffer = kmalloc(1024, GFP_KERNEL);
    if (!kernel_buffer) {
        device_destroy(tflite_parser_class, MKDEV(major_number, 0));
        class_destroy(tflite_parser_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        printk(KERN_ALERT "Failed to allocate memory for kernel buffer\n");
        return -ENOMEM;
    }

    return 0;
}

static void __exit tflite_parser_device_exit(void) {
    kfree(kernel_buffer);
    device_destroy(tflite_parser_class, MKDEV(major_number, 0));
    class_unregister(tflite_parser_class);
    class_destroy(tflite_parser_class);
    unregister_chrdev(major_number, DEVICE_NAME);
    printk(KERN_INFO "TFLiteParserDevice: Goodbye from the TFLiteParserDevice\n");
}

module_init(tflite_parser_device_init);
module_exit(tflite_parser_device_exit);
