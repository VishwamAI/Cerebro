#!/bin/bash

# Test script for TensorFlow model execution within the custom kernel

# Load the TensorFlow model into the kernel
echo "LOAD_MODEL /home/ubuntu/Cerebro/models/sample_model.tflite" > /dev/tensorflow_interpreter_device
if [ $? -ne 0 ]; then
    echo "Error: Failed to load model" >&2
    exit 1
fi

# Execute the TensorFlow model
echo "EXECUTE_MODEL" > /dev/tensorflow_interpreter_device
if [ $? -ne 0 ]; then
    echo "Error: Failed to execute model" >&2
    exit 1
fi

# Retrieve the results of the computation
echo "GET_RESULTS" > /dev/tensorflow_interpreter_device
if [ $? -ne 0 ]; then
    echo "Error: Failed to get results" >&2
    exit 1
fi

# Read the results from the device
cat /dev/tensorflow_interpreter_device
if [ $? -ne 0 ]; then
    echo "Error: Failed to read results" >&2
    exit 1
fi
