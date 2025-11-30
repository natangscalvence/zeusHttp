

<img width="505" height="180" alt="zeus" src="https://github.com/user-attachments/assets/e56d6796-2dae-469f-8dc0-25f0c82529d1" />

# zeusHTTP - High-Performance C Web Server Library

## Project Philosophy

zeusHTTP is an ambitious project to build a minimalist, high-performance, and highly secure HTTP server library written entirely in C.

The core philosophy is **Zero Overhead** and **Maximum Control** over system resources, prioritizing kernel-level efficiency and memory safety. The goal is to rival modern servers such as Apache and Nginx while remaining lightweight and developer-oriented.

<p align="center">
  <img src="https://img.shields.io/badge/Language-C11%20Pure-blue?style=flat&logo=c" alt="Language: C11">
</p>

---

## Current Status (Phase 1 Complete)

The server core is stable and functional, with essential high-performance and security features.

### Core I/O and Concurrency

- **Master Worker Model:** A Master Process supervises Worker Processes, restarting them on failure to ensure resilience and optimal multi-core utilization.
- **Asynchronous I/O Engine:** Powered by `epoll(7)` for a fully non-blocking I/O model.
- **Zero Copy File Serving:** Uses `sendfile(2)` for static file delivery without unnecessary user-space copying.
- **Streaming HTTP Parsing:** A state-machine-based parser (`http_parser_run`) handles incremental input safely and efficiently.

### Security

- **Privilege Drop:** Uses `setuid`/`setgid` to immediately drop from root to the unprivileged `zeushttp` user after binding the port, minimizing attack surface.

---

## Build and Usage

### Requirements

- Modern C compiler (GCC recommended)
- Standard Linux development headers
- `libbsd-dev` if required for certain system calls

### Compilation

From the project root:

```bash
make clean
make
```

## Execution and Testing

### Running the Server


Start the Master process on a port (e.g., 8080). If running on a privileged port (e.g., 80), you must use `sudo` for the process to bind the socket.

```bash
./zeushttp
```

# Test Handlers

## Basic Handler
```bash
curl http://127.0.0.1:8080/
```

## Zero Copy File Test
```bash
curl http://127.0.0.1:8080/file
```
