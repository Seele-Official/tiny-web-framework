#include <cstdint>
#include <csignal>
#include <utility>
#include <filesystem>
#include "log.h"

#include "web/loop.h"
#include "web/mime.h"

#include "coro/simple_task.h"
#include "coro/thread.h"

#include "web/response.h"
#include "web/routing.h"

namespace web::loop {
using namespace seele;
using namespace std::literals;
coro::simple_task async_handle_connection(int fd, ip::v4 a) {
    io::fd fd_w(fd);
    ip::v4 client_addr = a;
    co_await coro::thread::dispatch_awaiter{};

    char read_buffer[8192];

    auto timeout = 200ms;

    while (true) {
        auto parser = http::request::parser{};

        while (parser.empty()) {
            int32_t bytes_read = co_await io::awaiter::link_timeout{
                io::awaiter::read{fd_w.get(), read_buffer, sizeof(read_buffer)},
                timeout
            };

            if (bytes_read <= 0) {
                log::async::error("Failed to read from {}: {}", 
                    client_addr.to_string(), io::error::msg
                );
                co_return;
            } else {
                parser.feed({
                    read_buffer, 
                    static_cast<size_t>(bytes_read)
                });
            }
        }

        if (auto result = parser.pop_front(); result.has_value()) {

            auto& request = result.value();

            if (auto it = request.header.find("Connection"); it != request.header.end()) {
                if (it->second == "close") {
                    co_return; // Close connection immediately
                } else if (it->second == "keep-alive") {
                    timeout = 1000ms; // Keep-alive timeout
                }
            }

            co_await routing::detail::route(request)
                        .settings(fd_w.get(), client_addr, timeout);
            // TODO: Handle errors in response sending
            // For now, we assume response sending is always successful


        } else {
            log::async::error("Failed to parse request from {}", client_addr.to_string());
            co_await web::response::error(http::response::status_code::bad_request)
                        .settings(fd_w.get(), client_addr, timeout);
            co_return;
        }

    }
}
    
coro::simple_task server_loop(int32_t f) {
    int32_t fd = f;
    co_await coro::thread::dispatch_awaiter{};
    while(true){
        sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

        int32_t ret = co_await 
            io::awaiter::accept{fd, (sockaddr *)&client_addr, &client_addr_len};
        switch (ret) {
            case io::error::SYS:
            case io::error::CTX_CLOSED:
                log::async::error("Failed to accept connection: {}", io::error::msg);
                co_return;
            case io::error::TIMEOUT:
                log::async::debug("Accept timed out, retrying...");
                continue;
            default:
                break;
        
        }
        auto ipv4_addr = ip::v4::from_sockaddr_in(client_addr);
        log::async::info("Fd[{}]: Accepted connection from {}", fd, ipv4_addr.to_string());
        async_handle_connection(ret, ipv4_addr);
    }

    co_return;
}



coro::simple_task cancel(int32_t fd) {
    if (co_await io::awaiter::cancel_fd{fd} != 0){
        std::println("Failed to cancel fd: {}", fd);
    }
}



void add_static_file_router(){
    auto root = std::filesystem::absolute(routing::env::root_path());
    
    if (!std::filesystem::exists(root)) {
        std::println("Root path does not exist: {}", routing::env::root_path().string());
        std::terminate();
    }

    if (!std::filesystem::is_directory(root)) {
        std::println("Root path is not a directory: {}", routing::env::root_path().string());
        std::terminate();
    }

    routing::env::static_routers().reserve(512);
    // Recursively iterate through the directory and add files to the static router
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file()) {
            auto full_path = std::filesystem::absolute(entry.path());
            

            auto fd  = io::fd::open_file(full_path, O_RDONLY);

            if (!fd.is_valid()) {
                std::println("Failed to open file: {}", full_path.string());
                std::terminate();
            }

            int64_t file_size = io::get_file_size(fd);
            if (file_size < 0) {
                std::println("Failed to get file size for {}", full_path.string());
                std::terminate();
            }
            if (file_size == 0) {
                std::println("File is empty: {}", full_path.string());
                continue;
            }
            

            std::string content_type = "application/octet-stream";
            std::string ext = full_path.extension().string();
            if (auto it = web::mime_types.find(ext);it != web::mime_types.end()) {
                content_type = it->second;
            }

            auto relative_path_str = std::format("/{}", full_path.lexically_relative(root).c_str());
            log::sync::info("Adding file `{}` to router as `{}`", full_path.string(), relative_path_str);

            routing::env::static_routers().push_back(
                {
                    routing::env::file_head_router{
                        relative_path_str,
                        content_type,
                        (size_t)file_size
                    },
                    routing::env::file_router{
                        relative_path_str, 
                        content_type,
                        {(size_t)file_size, PROT_READ, MAP_SHARED, fd.get(), 0}
                    }
                }
            );

        }
    }

    // Register static file routers
    for (auto& [head_router, router] : routing::env::static_routers()) {
        log::sync::info("Adding file router: {}", router.name);
        routing::get(router.name, router);
        routing::head(head_router.name, head_router);
    }
}

void run(){
    if (!env::listen_addr().is_valid()){
        std::println("Server address is not valid");
        std::terminate();
    }

    add_static_file_router();


    for (auto _ : std::views::iota(0uz, env::worker_count())){
        auto fd = io::fd::open_socket(
            env::listen_addr().to_sockaddr_in(), 
            env::max_worker_conn(), 
            [](int fd){
                setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &static_cast<const int&>(1), sizeof(int));
            }
        );

        if (!fd.is_valid()) {
            std::println("Failed to setup socket: {}", strerror(errno));
            std::terminate();
        }
        env::accepter_fds().push_back(std::move(fd));
    }

    for (auto& fd: env::accepter_fds()){
        server_loop(fd.get());
    }


    std::signal(SIGINT, [](int) {
        std::println("Received SIGINT, stopping server...");
        for (auto& fd: env::accepter_fds()){
            cancel(fd.get());
        }
        // TODO : Graceful shutdown
        std::atomic_thread_fence(std::memory_order_seq_cst);
        io::request_stop(); 
    });

    io::run();
}



}