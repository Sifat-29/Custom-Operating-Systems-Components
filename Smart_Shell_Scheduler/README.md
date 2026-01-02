# Smart_Shell_Scheduler: A Signal-Driven Multiprocessing CPU Scheduler with a Custom Shell as User Interface

A project that implements a background daemon to orchestrate the execution of multiple processes. By leveraging **Round-Robin scheduling**, the system ensures fair CPU distribution across a configurable number of virtual cores, managing process synchronisation via a unique signal-based "Virtual Halt" mechanism.

## Project Overview

The SimpleScheduler toolchain consists of a **User Shell**, a **Scheduling Daemon** and a **Synchronisation Header**. The primary objective is to execute computational workloads under strict resource constraints (NCPU) and specific time quanta (TSLICE). This mimics the operational logic of an Operating System's scheduler, dealing with process state transitions, queue management and performance analytics.

## System Architecture

The project is architected into three distinct layers:
*   **SimpleShell (The Producer):** A command-line interface where users submit jobs. It forks child processes in a "suspended" state and communicates their PIDs to the scheduler via an asynchronous Unix Pipe.
*   **SimpleScheduler (The Consumer/Controller):** A daemon that maintains three internal queues: `READY`, `RUNNING`, and `COMPLETED`. It manages the Fetch-Decode-Execute cycle by signaling processes to resume or pause.
*   **DummyMain (The Synchronization Wrapper):** A custom header (`dummy_main.h`) that redefines the `main` function. It forces every submitted job to raise `SIGSTOP` immediately upon entry, ensuring the scheduler has absolute control over the process's first instruction.

## Technical Implementation

### Round-Robin & Signal Control
The scheduler implements a Round-Robin algorithm with a configurable time slice. At the end of every `TSLICE`:
1.  The scheduler sends `SIGSTOP` to all processes in the `RUNNING` queue.
2.  It re-queues them into the `READY` pool.
3.  It selects the next $N$ processes and sends `SIGCONT` to resume their execution.

### Asynchronous IPC
Communication between the Shell and Scheduler is handled via **non-blocking Pipes**. The Shell pushes job PIDs into the submission pipe, while the Scheduler pushes detailed performance results into a result pipe upon job completion or system shutdown.

### Process Lifecycle Management
The system handles "Zombie" processes by monitoring `/proc/[pid]/stat`. It distinguishes between processes that are genuinely blocked and those that have terminated, ensuring that completed jobs are reaped and their metrics are accurately recorded.

## Performance Metrics & Analytics
Upon system termination, the scheduler reports precise telemetry for every executed job:
*   **Run Slices:** The number of time quanta the process spent in the `RUNNING` state.
*   **Wait Slices:** The number of time quanta spent in the `READY` queue.
*   **Total Completion Time:** The aggregate wall-clock time from submission to termination.

## Usage Specification

### Compilation
The project uses a structured project layout. Build using:
```bash
make
```

### Execution
Start the shell with 4 virtual CPUs and a 100ms time slice:
```bash
# Usage: ./bin/simpleShell [NCPU] [TSLICE_MS]
./bin/simpleShell 4 100
```

### Job Submission
```bash
user@shell$ submit ./bin/fib43 ./bin/fib44
```
