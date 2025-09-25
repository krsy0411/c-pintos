# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a KAIST Pintos operating system project running on 64-bit x86-64 architecture with Ubuntu 22.04. The project is divided into three main phases:
- **threads** (9th week): Thread management and synchronization
- **userprog** (10-11th weeks): User program execution and system calls
- **vm** (12-13th weeks): Virtual memory management
- **filesys**: File system implementation

## Development Environment

The project uses Docker with VSCode DevContainer for consistent development environment:
- Environment activation: `source /workspaces/pintos_22.04_lab_docker/pintos/activate` (automatically runs in VSCode terminal)
- The activate script adds `pintos/utils` to PATH for pintos commands

## Build Commands

### Building modules
```bash
# Build specific module (from repository root)
make -C pintos/threads
make -C pintos/userprog
make -C pintos/vm
make -C pintos/filesys

# Clean build
make -C pintos/threads clean
make -C pintos/userprog clean
make -C pintos/vm clean
```

### Running tests
Each module has a `select_test.sh` script for interactive testing:
```bash
# Run tests in batch mode (quiet, no GDB)
./pintos/threads/select_test.sh -q

# Run tests in debug mode (with GDB stub at localhost:1234)
./pintos/userprog/select_test.sh -g

# Force clean rebuild before testing
./pintos/vm/select_test.sh -q -r
```

The test scripts:
- Parse `.test_config` files for test configurations
- Allow selecting multiple tests by number or range (e.g., "1 3 5" or "2-4")
- Track test results in `.test_status` files
- Support both batch execution and interactive debugging

## Project Architecture

### Core Directories
- **pintos/threads/**: Thread scheduling, synchronization primitives (mutexes, semaphores, condition variables)
- **pintos/userprog/**: Process execution, system call handling, user program loading
- **pintos/vm/**: Virtual memory management, page tables, swap management
- **pintos/filesys/**: File system operations and disk management
- **pintos/devices/**: Hardware device drivers (timer, keyboard, etc.)
- **pintos/lib/**: Shared utility functions and data structures
- **pintos/include/**: Header files organized by subsystem

### Build System
- Top-level `Makefile` coordinates builds across modules
- Each module uses `../Makefile.kernel` for consistent build rules
- `Make.config` defines compiler flags and settings:
  - Uses GCC with debugging symbols (-g)
  - Disables optimization (-O0) for easier debugging
  - 64-bit compilation with specific x86-64 flags

### Testing Framework
- Tests located in `pintos/tests/` with subdirectories per module
- Each test has a corresponding `.ck` Perl script for result validation
- Test execution uses the `pintos` utility (in `utils/`) to run kernel in QEMU
- Results are validated by checking for "PASS" in `.result` files

## Debugging

- GDB debugging available when using `-g` flag with test scripts
- GDB connects to QEMU's stub at `localhost:1234`
- Debug configurations available in `launch.json`
- Use `pintos --gdb` for manual kernel debugging sessions

## Key Utilities

- **pintos/utils/pintos**: Main utility for running Pintos kernel in QEMU
- **pintos/utils/pintos-mkdisk**: Creates disk images for file system testing
- **pintos/utils/backtrace**: Analyzes kernel backtraces for debugging