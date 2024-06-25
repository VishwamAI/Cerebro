#!/bin/bash

# Test script for TensorFlow model execution within the custom kernel

# Log system resource usage
echo "Logging system resource usage before loading model..."
free -m
ulimit -a
lsof | wc -l

# Load the TensorFlow model into the kernel
echo "LOAD_MODEL /home/ubuntu/Cerebro/models/mobilenet_v1_1.0_224_quant.tflite" > /dev/tensorflow_interpreter_device
if [ $? -ne 0 ]; then
    echo "Error: Failed to load model" >&2
    exit 1
fi

# Log system resource usage
echo "Logging system resource usage after loading model..."
free -m
ulimit -a
lsof | wc -l

# Execute the TensorFlow model
echo "EXECUTE_MODEL" > /dev/tensorflow_interpreter_device
if [ $? -ne 0 ]; then
    echo "Error: Failed to execute model" >&2
    exit 1
fi

# Log system resource usage
echo "Logging system resource usage after executing model..."
free -m
ulimit -a
lsof | wc -l

# Retrieve the results of the computation
echo "GET_RESULTS" > /dev/tensorflow_interpreter_device
if [ $? -ne 0 ]; then
    echo "Error: Failed to get results" >&2
    exit 1
fi

# Log system resource usage
echo "Logging system resource usage after getting results..."
free -m
ulimit -a
lsof | wc -l

# Read the results from the device
cat /dev/tensorflow_interpreter_device
if [ $? -ne 0 ]; then
    echo "Error: Failed to read results" >&2
    exit 1
fi

# Log system resource usage after reading results
echo "Logging system resource usage after reading results..."
free -m
ulimit -a
lsof | wc -l

# Additional logging for debugging
echo "Test script completed successfully"
