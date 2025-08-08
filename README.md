# Tiny Webserver

## Introduction

一个简易 webserver。

## Features

- **C++23:** 
- **Proactor Pattern:**

## Requirements

- GCC 14+
- [liburing](https://github.com/axboe/liburing)
- CMake

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

搭建一个简易 GET 服务

```cpp
#include <print>
#include "http.h"
#include "log.h"
#include "server.h"
using namespace seele;

int main() {
    log::logger().set_output_file("web_server.log");
    auto tiny_app = [](const http::query_t& query, const http::header_t& header) -> web::handler_response {
        std::println("Received GET request for /tiny_app with query: {}", query);
        if (query != "hello!"){
            return web::send_http_error(http::status_code::bad_request);
        }
        std::println("Headers:");
        for (const auto& [key, value] : header) {
            std::println("  {}: {}", key, value);
        }

        return web::send_msg(
            {
                http::status_code::ok,
                {
                    {"Content-Type", "text/plain; charset=utf-8"}
                },
                "Hello from tiny_get_app!"
            }
        );
    };


    app().set_addr("127.0.0.1:80")
        .set_root_path("/home/seele/webserver/static")
        .GET("/tiny_app.so", tiny_app)
        .run();
    return 0;
}
```
