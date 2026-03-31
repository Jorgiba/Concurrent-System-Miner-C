# Concurrent Mining System: High-Performance IPC in C

## Project Overview
This project is a low-level concurrent system developed in **C** for Linux/POSIX environments. It simulates a distributed mining architecture where multiple worker processes (**Miners**) solve computational puzzles coordinated by a central **Monitor** process.

## System Architecture
The system demonstrates Operating Systems concepts through a multi-process design:
* **Monitor Process:** Acts as the orchestrator. It initializes shared resources, manages the blockchain state, and handles system-wide synchronization.
* **Miner Processes:** Intensive CPU workers that perform "Proof of Work" (PoW) calculations.

## Key Engineering Features
* **Inter-Process Communication (IPC):** Implemented using **POSIX Shared Memory** (`shm_open`, `mmap`) for high-speed data exchange.
* **Complex Synchronization:** Orchestrated via **Semaphores** to prevent race conditions during concurrent access to the shared blockchain state.
* **Robust Signal Handling:** Custom signal masking and handling for graceful process termination and state updates.
* **Automated Build System:** Includes a `Makefile` for modular compilation and dependency management.

## Tech Stack
* **Language:** C 
* **Environment:** Linux / POSIX API
* **Key APIs:** `pthreads` (if used), `sys/shm`, `sys/sem`, `signal.h`.

## Execution
To build the project:
```bash
./monitor [parameters]
```
