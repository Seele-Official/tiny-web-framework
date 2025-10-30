#include <cstdint>
#include <csignal>
#include <string_view>
#include <utility>
#include "logging/log.h"

#include "web/loop.h"

#include "coro/simple_task.h"
#include "coro/thread.h"

#include "web/response.h"
#include "web/routing.h"

namespace web::loop {
namespace env {
std::vector<io::fd>& accepter_fds(){
    static std::vector<io::fd> accepter_fds{};
    return accepter_fds;
}
}



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
                logging::async::error("Failed to read from {}: {}", 
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
                        .settings({fd_w.get(), client_addr, timeout});
            // TODO: Handle errors in response sending
            // For now, we assume response sending is always successful


        } else {
            logging::async::error("Failed to parse request from {}", client_addr.to_string());
            co_await web::response::error(http::response::status_code::bad_request)
                        .settings({fd_w.get(), client_addr, timeout});
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
                logging::async::error("Failed to accept connection: {}", io::error::msg);
                co_return;
            case io::error::TIMEOUT:
                logging::async::debug("Accept timed out, retrying...");
                continue;
            default:
                break;
        
        }
        auto ipv4_addr = ip::v4::from_sockaddr_in(client_addr);
        logging::async::info("Fd[{}]: Accepted connection from {}", fd, ipv4_addr.to_string());
        async_handle_connection(ret, ipv4_addr);
    }

    co_return;
}



coro::simple_task cancel(int32_t fd) {
    if (co_await io::awaiter::cancel_fd{fd} != 0){
        std::println("Failed to cancel fd: {}", fd);
    }
}



void run(){
    if (!env::listen_addr().is_valid()){
        std::println("Server address is not valid");
        std::terminate();
    }

    routing::configure_static_resource_routes();


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