# Tiny Webserver

## Introduction

The Tiny Webserver is a lightweight, high-performance server built with modern C++23. It leverages advanced C++ features to deliver an efficient and robust solution for handling web requests. With its focus on performance, the server implements asynchronous operations using coroutines, ensuring non-blocking I/O and high scalability.

## Features

- **Modern C++23:** Utilizes new language features such as coroutines for asynchronous operations and advanced template meta-programming for compile-time optimizations.
- **Proactor Pattern:** Handles asynchronous I/O operations efficiently using the proactor design, implemented with [liburing](https://github.com/axboe/liburing).
- **Lock-Free Structures:** Employs lock-free data structures to reduce contention and improve concurrency.
- **Thread Pool:** Manages multiple threads to handle workloads concurrently, ensuring smooth performance under high load.

## Requirements

- A C++23 compliant compiler (e.g., GCC 14+).
- [liburing](https://github.com/axboe/liburing) for asynchronous I/O operations.
- CMake.

## Installation and Build

1. **Clone the repository:**
   ```sh
   git clone https://github.com/Seele-Official/webserver.git
   cd webserver
   ```

2. **Build using CMake:**
   ```sh
   mkdir build && cd build
   cmake ..
   cmake --build .
   ```

## Usage

After building, you can run the server:

```sh
./web_server -a 127.0.0.1:8080 -p /home/seele/webserver/static
```

The server will listen for HTTP requests on the specified port.
