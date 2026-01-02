# Custom-Operating-Systems-Components

This repository contains a collection of pure C implementations designed to explore the core architectural primitives operating systems. The projects focus on three primary domains: **Virtual Memory Management**, **Process Orchestration**, and **Concurrent Execution**.

All components are developed in C using POSIX standards, focusing on efficient resource management, hardware-level synchronisation and proper error handling.

---

## Repository Structure

```text
.
├── Smart_Loader/               # Elf Loading, Virtual Memory Management & Demand Paging
├── Smart_Shell_Scheduler/      # CPU Scheduling with an integrated Shell
├── Thread_Pool_API/            # Concurrency & Synchronised Workloads
└── README.md       
```

---

## Project Specifications

### 1. Custom ELF Loader with Page Replacement
**Focus:** Elf Loading, Virtual Memory Management & Demand Paging

An implementation of a user-space memory manager that executes 32-bit ELF binaries using demand paging. Rather than utilising eager loading, the system intercepts hardware-level page faults to map memory segments dynamically.

*   **Signal-Based Paging:** Uses `SIGSEGV` interception via the `sigaction` API to detect and resolve unmapped memory access in real-time.
*   **Replacement Algorithms:** Implements FIFO and Random replacement strategies to manage execution under strict physical memory constraints (max page limits).
*   **Secondary Storage Swapping:** Features a Swap Manager to persist evicted writable segments to an on-disk backing store (`swap.img`), ensuring data integrity during high memory pressure.

### 2. Round Robin Scheduler Integrated with a Shell
**Focus:** CPU Scheduling with integrated Shell (Process Management and IPC)

A multiprocessing suite consisting of a command-line shell and a background scheduling daemon. The system implements a Round-Robin algorithm to manage the execution of multiple concurrent processes across a configurable number of virtual CPU cores.

*   **Signal-Driven Scheduling:** Controls process lifecycles via `SIGSTOP` and `SIGCONT` to manage state transitions between `READY` and `RUNNING` queues.
*   **Asynchronous IPC:** Employs non-blocking Unix pipes for job submission and performance telemetry reporting (run-slices, wait-slices) between the shell and daemon.
*   **Virtual Halt Synchronization:** Uses a custom entry-point wrapper (`dummy_main.h`) to force processes into a suspended state immediately upon creation until formally dispatched.

### 3. Thread Pool API
**Focus:** Concurrency and Synchronisation

A high-performance library that implements the worker-pool pattern to mitigate the overhead associated with frequent thread creation and destruction in multi-threaded applications.

*   **Synchronised Job Queue:** Features a thread-safe FIFO queue protected by mutexes and condition variables to prevent race conditions.
*   **Thread Lifecycle Management:** Maintains a persistent pool of worker threads that utilize `pthread_cond_wait` for efficient idle sleep/wake cycles.
*   **Graceful Shutdown:** Implements proper cleanup protocols to ensure all pending tasks are completed and thread resources are reclaimed without any memory leaks.
