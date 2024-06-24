# Windows to Linux System Call Translation

This document provides a mapping of key Windows file operation functions to their Linux equivalents. This mapping is essential for implementing the system call translation layer in the custom kernel, allowing .exe files to execute within the Linux environment.

## File Operations

### CreateFile
- **Windows Function**: `CreateFile`
- **Linux Equivalent**: `open`, `creat`
- **Description**: The `CreateFile` function in Windows is used to create or open a file. In Linux, the equivalent operations are performed using the `open` or `creat` system calls.

### ReadFile
- **Windows Function**: `ReadFile`
- **Linux Equivalent**: `read`
- **Description**: The `ReadFile` function in Windows reads data from a file. The equivalent operation in Linux is performed using the `read` system call.

### WriteFile
- **Windows Function**: `WriteFile`
- **Linux Equivalent**: `write`
- **Description**: The `WriteFile` function in Windows writes data to a file. The equivalent operation in Linux is performed using the `write` system call.

### CloseHandle
- **Windows Function**: `CloseHandle`
- **Linux Equivalent**: `close`
- **Description**: The `CloseHandle` function in Windows closes an open object handle, such as a file handle. The equivalent operation in Linux is performed using the `close` system call.

### DeleteFile
- **Windows Function**: `DeleteFile`
- **Linux Equivalent**: `unlink`
- **Description**: The `DeleteFile` function in Windows deletes a file. The equivalent operation in Linux is performed using the `unlink` system call.

### CreateDirectory
- **Windows Function**: `CreateDirectory`
- **Linux Equivalent**: `mkdir`
- **Description**: The `CreateDirectory` function in Windows creates a new directory. The equivalent operation in Linux is performed using the `mkdir` system call.

### RemoveDirectory
- **Windows Function**: `RemoveDirectory`
- **Linux Equivalent**: `rmdir`
- **Description**: The `RemoveDirectory` function in Windows removes an existing directory. The equivalent operation in Linux is performed using the `rmdir` system call.

## Additional Notes
- The translation layer must handle different flags, modes, and other parameters that may not have direct equivalents between Windows and Linux.
- Error handling and edge cases must be carefully considered to ensure accurate translation and robust operation.

This document will be updated as additional mappings are identified and implemented.
