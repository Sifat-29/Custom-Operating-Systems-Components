# Asynchronous Thread Pool API

A synchronised worker-pool implementation that facilitates parallel processing in C. The library provides a structured interface for offloading computational tasks to a background pool utilizing synchronisation to ensure thread safety and data integrity.

## Project Overview

In multi-threaded applications, the overhead of repeatedly spawning and joining threads is detrimental to performance. This project implements a **Thread Pool** in which a fixed number of worker threads are initialised once and wait for tasks to be pushed into a shared queue.

## Technical Architecture

### Concurrency and Synchronization
The library is built upon the **POSIX Threads (Pthreads)** standard, using a synchronisation model to prevent race conditions:
*   **Mutual Exclusion (Mutex):** A global `pthread_mutex_t` protects the job queue, ensuring that only one thread can modify the queue (push or pop) at a time.
*   **Condition Variables:**
    *   `COND_WORKER`: Used to block worker threads when the queue is empty, preventing "busy-waiting" and reducing CPU consumption.
    *   `COND_COMPLETED`: Acts as a synchronisation barrier, allowing the main thread to block until all pending jobs in the pool are finished.

### Tasks Management
*   **FIFO Job Queue:** Tasks are managed via a singly-linked list structure. Jobs are executed in the order they are submitted (First-In, First-Out).
*   **Generic Task Interface:** The API accepts a function pointer (`void (*)(void*)`) and a generic `void*` argument, allowing the pool to execute any arbitrary logic.

### Lifecycle Management
*   **Graceful Shutdown:** The `thread_pool_cleanup` routine ensures a clean exit by setting a shutdown flag, broadcasting to all sleeping workers and then joining each thread to reclaim system resources and prevent memory leaks.
*   **Task Persistence:** The pool ensures that even if a shutdown is requested, workers will not exit until they have finished processing the current job they have popped from the queue.

## Usage Specification

### API Overview
*   `thread_pool_init(int n)`: Spawns $n$ worker threads and prepares the synchronisation primitives.
*   `thread_pool_add_job(func, args)`: Encapsulates a function and its arguments into a `Job` struct and pushes it to the synchronised queue.
*   `thread_pool_wait()`: Blocks the calling thread until the `JOBS_PENDING` counter reaches zero.
*   `thread_pool_cleanup()`: Deallocates all internal structures and joins the worker threads.

### Compilation
The library must be linked with the `lpthread` flag:
```bash
gcc -o my_app main.c thread_pool.c -lpthread
```
