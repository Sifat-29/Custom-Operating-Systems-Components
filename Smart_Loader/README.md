# Custom ELF Loader with Page Replacement and Swapping

A memory management toolchain that implements a user-space virtual memory manager. This project demonstrates the mechanics of lazy loading, demand paging and secondary storage swapping to mirror the core memory management subsystems of modern operating systems.

## Project Overview

The loader serves as a controlled execution environment for 32-bit ELF (Executable and Linkable Format) binaries. Unlike standard loaders that map segments into memory upfront, this implementation uses **Demand Paging**. It allows programs to run with a physical memory footprint significantly smaller than their logical size by dynamically loading and evicting pages based on real-time execution requirements.

## Architectural Components

### Lazy Loading & Signal Handling
The system uses the `SIGSEGV` (Segmentation Fault) signal to identify memory access violations at the hardware level.
*   **Intercept Subsystem:** Uses the `sigaction` API with the `SA_SIGINFO` flag to capture the exact faulting virtual address.
*   **On-Demand Mapping:** Upon a fault, the handler identifies the corresponding ELF segment, calculates the file offset and maps exactly one **4KB page** using `mmap` with the `MAP_FIXED` flag to satisfy the specific virtual memory requirement.

### Page Replacement Policies
To handle execution under strict memory constraints (specified via the `max_pages` argument), the loader implements two distinct replacement algorithms to manage the "Physical Memory" pool:
*   **FIFO (First-In-First-Out):** Uses a queue-based structure to track the order of page allocations and evicting the oldest page in the pool when the limit is reached.
*   **Random:** Selects a victim page for eviction using a pseudo-random generator, providing a non-deterministic baseline for performance comparison against deterministic strategies.

###  Swap Management System
When a page is evicted to satisfy a memory limit, the **Swap Manager** ensures data persistence and process integrity:
*   **Backing Store:** Evicted pages are serialized to an on-disk swap image (`swap.img`).
*   **Swap-In Logic:** During a page fault, the loader first queries the **Swap Table**. If the requested page exists in the swap file, it is restored to physical memory to preserve any runtime changes; otherwise, it is loaded from the original ELF binary.
*   **Selective Swapping:** The system optimises performance by discarding read-only pages (Text segment) during eviction while strictly swapping writable pages (Data/BSS segments).

---

## 3. Technical Specifications

### Toolchain Requirements
*   **Environment:** Linux-based shell (32-bit support required via `gcc-multilib`).
*   **Compilation Flags:**
    *   `-m32`: Ensures 32-bit architecture compatibility.
    *   `-fPIC -shared`: Generates the loader as a position-independent shared library.
    *   `-nostdlib -no-pie`: Used for test cases to provide a raw entry point (`_start`) and stable virtual addresses.

---

## Usage Specification

### Building the Toolchain
The project uses a recursive Makefile structure to build the loader library, the launcher utility, and the test suite.
```bash
make
```

### Executing a Program
Run a target ELF binary by specifying the replacement policy and the maximum physical memory limit (in pages):
```bash
# Usage: ./bin/launch <Binary_Path> <Policy: FIFO/RANDOM> <Max_Pages>
./bin/launch ./test/linear_access FIFO 3
```

### Statistics Reporting
Upon completion, the loader outputs a performance report including:
*   **Page Faults:** Total number of `SIGSEGV` signals handled.
*   **Page Allocations:** Total number of unique `mmap` calls.
*   **Page Evictions:** Number of times the replacement policy was triggered.
*   **Internal Fragmentation:** Precise byte-count of wasted physical memory across all resident pages.
