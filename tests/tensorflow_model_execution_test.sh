#!/bin/bash

# Test script for TensorFlow model execution within the custom kernel

# Load the TensorFlow model into the kernel
echo "LOAD_MODEL /home/ubuntu/Cerebro/models/sample_model.tflite" > /dev/tensorflow_interpreter_device

# Execute the TensorFlow model
echo "EXECUTE_MODEL" > /dev/tensorflow_interpreter_device

# Retrieve the results of the computation
echo "GET_RESULTS" > /dev/tensorflow_interpreter_device

# Read the results from the device
cat /dev/tensorflow_interpreter_device
