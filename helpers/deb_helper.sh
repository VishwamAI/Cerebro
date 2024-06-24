#!/bin/bash

# Deb Helper Script
# This script handles the installation and execution of .deb files in a secure, sandboxed environment.

# Function to install a .deb file
install_deb() {
    local deb_file=$1
    sudo dpkg -i "$deb_file"
    if [ $? -ne 0 ]; then
        echo "Failed to install $deb_file. Attempting to fix dependencies..."
        sudo apt-get install -f -y
    fi
}

# Function to execute a command in a sandboxed environment
execute_sandboxed() {
    local command=$1
    # Using bubblewrap for sandboxing
    bwrap --dev-bind / / --ro-bind /usr /usr --ro-bind /lib /lib --ro-bind /lib64 /lib64 --ro-bind /bin /bin --ro-bind /sbin /sbin --proc /proc --tmpfs /tmp --unshare-all -- "$command"
}

# Main script logic
if [ $# -lt 2 ]; then
    echo "Usage: $0 <deb_file> <command>"
    exit 1
fi

deb_file=$1
shift
command=$@

# Install the .deb file
install_deb "$deb_file"

# Execute the command in a sandboxed environment
execute_sandboxed "$command"
