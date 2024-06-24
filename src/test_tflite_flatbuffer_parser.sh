#!/bin/bash

# Test script for TensorFlow Lite FlatBuffer Parser

# Load the kernel module
sudo insmod tflite_flatbuffer_parser.ko

# Check if the module was loaded successfully
if lsmod | grep -q "tflite_flatbuffer_parser"; then
    echo "Kernel module loaded successfully."
else
    echo "Failed to load kernel module."
    exit 1
fi

# Test case 1: Valid TensorFlow Lite model with ADD operator
echo "Running test case 1: Valid TensorFlow Lite model with ADD operator"
echo "PARSE_MODEL" > /dev/tflite_parser_device
# (Add commands to load and execute the model here)

# Test case 2: Valid TensorFlow Lite model with MUL operator
echo "Running test case 2: Valid TensorFlow Lite model with MUL operator"
echo "PARSE_MODEL" > /dev/tflite_parser_device
# (Add commands to load and execute the model here)

# Test case 3: Valid TensorFlow Lite model with SUB operator
echo "Running test case 3: Valid TensorFlow Lite model with SUB operator"
echo "PARSE_MODEL" > /dev/tflite_parser_device
# (Add commands to load and execute the model here)

# Test case 4: Invalid TensorFlow Lite model (e.g., incompatible tensor shapes)
echo "Running test case 4: Invalid TensorFlow Lite model (incompatible tensor shapes)"
echo "PARSE_MODEL" > /dev/tflite_parser_device
# (Add commands to load and execute the model here)

# Unload the kernel module
sudo rmmod tflite_flatbuffer_parser

# Check if the module was unloaded successfully
if lsmod | grep -q "tflite_flatbuffer_parser"; then
    echo "Failed to unload kernel module."
    exit 1
else
    echo "Kernel module unloaded successfully."
fi

echo "All test cases completed."
