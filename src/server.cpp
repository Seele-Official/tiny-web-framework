#include "server.h"


#include <cstdint>
#include <cstring>
#include <exception>
#include <expected>
#include <format>
#include <mutex>
#include <optional>
#include <print>
#include <string>
#include <cstddef>
#include <string_view>
#include <unordered_map>
#include <filesystem>
#include <csignal>

#include "meta.h"
#include "coro/task.h"
#include "log.h"
#include "http.h"
#include "coro_io.h"
#include "io.h"
#include "net/ipv4.h"

using std::literals::operator""s;
using std::literals::operator""ms;

using namespace seele;

namespace env {
    static std::filesystem::path root_path = std::filesystem::current_path() / "www";
    static net::ipv4 addr;
    static fd_wrapper fd_w;
}


namespace file_cache {
    static std::mutex mutex{};
    static std::unordered_map<std::string, file_mmap> files{};

    std::expected<iovec, http::error_code> get(const std::filesystem::path& path) {
        auto full_path = std::filesystem::absolute(path);
        if (auto it = files.find(path.string());it != files.end()) {
            return iovec{
                it->second.data,
                it->second.size
            };
        }
        return std::unexpected{http::error_code::not_found};
    }
    // std::expected<iovec, http::error_code> get(const std::filesystem::path& path) {
    //     auto full_path = std::filesystem::absolute(path);
    //     {
    //         std::lock_guard<std::mutex> lock(mutex);

    //         if (auto it = files.find(full_path.string());it != files.end()) {

    //             return iovec{
    //                 it->second.data,
    //                 it->second.size
    //             };

    //         } else {
    //             fd_wrapper file_fd_w(open(full_path.c_str(), O_RDONLY));
    //             if (!file_fd_w.is_valid()) {
    //                 if (errno == EACCES || errno == EPERM) {
    //                     return std::unexpected{http::error_code::forbidden};
    //                 }
    //                 return std::unexpected{http::error_code::not_found};
    //             }


    //             int64_t file_size = get_file_size(file_fd_w);
    //             if (file_size < 0) {
    //                 log::async().error("Failed to get file size for {}", full_path.string());
    //                 return std::unexpected{http::error_code::internal_server_error};
    //             }

    //             file_mmap f(file_size, PROT_READ, MAP_SHARED, file_fd_w, 0);
                
    //             iovec i{
    //                 f.data,
    //                 f.size
    //             };

    //             files.emplace(full_path.string(), std::move(f));
    //             return i;
    //         }
    //     }




    // }

}




struct http_file_ctx{
    iovec_wrapper header;
    iovec data;
    static http_file_ctx make(const http::res_msg& msg, void* file, size_t size) {
        http_file_ctx res{
            {256},
            {file, size}
        };
        auto it = msg.format_to(static_cast<char*>(res.header.iov_base));
        res.header.iov_len = meta::safe_cast<size_t>(it - static_cast<char*>(res.header.iov_base));
        return res;
    }
};

std::expected<http_file_ctx, http::error_code> handle_get_req(const http::req_msg& req) {

    auto origin = std::get_if<http::origin_form>(&req.line.target);
    if (!origin) {
        return std::unexpected{http::error_code::not_implemented};
    }

    std::filesystem::path origin_path = origin->path;
    std::filesystem::path full_path = env::root_path / origin_path.relative_path();

    std::filesystem::path rel = full_path.lexically_relative(env::root_path);

    if (rel.empty() || *rel.begin() == ".."){
        log::async().error("Attempted to access outside of root path: {}", full_path.string());
        return std::unexpected{http::error_code::forbidden};
    }

    if (std::filesystem::is_directory(full_path)) {
        full_path /= "index.html";
    }

    if(auto ctx = file_cache::get(full_path);!ctx.has_value()){
        return std::unexpected{ctx.error()};
    } else {

        std::string content_type = "application/octet-stream";
        std::string ext = full_path.extension().string();

        if (auto it = http::mime_types.find(ext);it != http::mime_types.end()) {
            content_type = it->second;
        }
        return http_file_ctx::make(
            {
                {200, "OK"},
                {
                    {"Content-Type", content_type},
                    {"X-Content-Type-Options", "nosniff"},
                    {"Content-Length", std::to_string(ctx.value().iov_len)},
                },
                std::nullopt
            }, 
            ctx.value().iov_base, 
            ctx.value().iov_len
        );
    }
}




struct send_http_error {
    http_file_ctx ctx;
    io_link_timeout_awaiter<io_writev_awaiter> awaiter;

    auto await_ready() { return awaiter.await_ready(); }
    auto await_suspend(std::coroutine_handle<> handle) {
        return awaiter.await_suspend(handle);
    }
    auto await_resume() {return awaiter.await_resume();}
    send_http_error(int fd, http::error_code code){
        auto content = http::error_contents[code];
        this->ctx = http_file_ctx::make(
            {
                {static_cast<size_t>(code), "ERROR"},
                {
                    {"Content-Type", "text/html; charset=utf-8"},
                    {"X-Content-Type-Options", "nosniff"},
                    {"Content-Length", std::to_string(content.size())}
                },
                std::nullopt
            },
            (void*) content.data(),
            content.size()
        );
        this->awaiter = io_link_timeout_awaiter{
            io_writev_awaiter{fd, &ctx.header, 2},
            5s
        };
    }
};


coro::task async_handle_connection(int fd, net::ipv4 addr) {
    fd_wrapper fd_w(fd);
    net::ipv4 client_addr = addr;
    log::async().info("Accepted connection from {}", client_addr.toString());
    co_await coro::thread::dispatch_awaiter{};

    char read_buffer[8192];
    std::optional<std::string_view> remain = std::nullopt;
    while (true) {
        auto parser = http::req_msg::parser();
        if (remain.has_value()) {
            parser.send_and_resume(remain.value());
            remain = std::nullopt;
        }
        while (!parser.done()) {       
            auto res = co_await io_link_timeout_awaiter{
                io_read_awaiter{fd_w, read_buffer, sizeof(read_buffer)},
                60s
            };
            
            if (!res.has_value()) {
                log::async().warn("Read timed out");
                co_return;
            }

            auto bytes_read = res.value().res;
            if (bytes_read == 0) {
                log::async().warn("Connection closed by client");
                co_return;
            }
            if (bytes_read < 0) {
                log::async().error("Failed to read from socket: {}", strerror(-bytes_read));
                co_return;
            }

            parser.send_and_resume({read_buffer, static_cast<size_t>(bytes_read)});
        }

        if (auto result = parser.get()) {
            auto& [msg, remain_opt] = result.value();
            remain = remain_opt;

            switch (msg.line.method) {
                case http::method_t::GET: {
                    auto get_res = handle_get_req(msg);
                    if (get_res.has_value()) {
                        auto res = co_await io_link_timeout_awaiter{
                            io_writev_awaiter{
                                fd_w,
                                &get_res.value().header,
                                2
                            },
                            5s
                        };

                        if (!res.has_value()) {
                            log::async().error("Failed to send response");
                            co_return;
                        }
                        if (res.value().res < 0) {
                            log::async().error("Failed to send response: {}", strerror(-res.value().res));
                            co_return;
                        }
                    } else {
                        co_await send_http_error{fd_w, get_res.error()};
                    }
                }
                break;
                default: {
                    co_await send_http_error{fd_w, http::error_code::method_not_allowed};
                    co_return;
                }
            
            }
        } else {
            log::async().error("Failed to parse request from {}", client_addr.toString());
            co_await send_http_error{fd_w, http::error_code::bad_request};
            co_return;            
        }

    }
}



coro::task server_loop() {

    co_await coro::thread::dispatch_awaiter{};

    while(true){
        sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

        auto res = co_await io_link_timeout_awaiter{
            io_accept_awaiter{env::fd_w, (sockaddr *)&client_addr, &client_addr_len},
            5s
        };

        if (!res.has_value()){
            log::async().info("Accept timed out or failed");
            continue;
        }
        if (res.value().res < 0) {
            log::async().error("Accept failed: {}", strerror(-res.value().res));
            break;
        }
        async_handle_connection(res.value().res, net::ipv4::from_sockaddr_in(client_addr));
    }

    co_return;
}








struct app& app::set_root_path(std::string_view path) {
    env::root_path = std::filesystem::absolute(path);

    for (const auto& entry : std::filesystem::recursive_directory_iterator(env::root_path)) {
        if (entry.is_regular_file()) {
            auto full_path = std::filesystem::absolute(entry.path());
            log::sync().info("Adding file to cache: {}", full_path.string());

            fd_wrapper file_fd_w(open(full_path.c_str(), O_RDONLY));
            if (!file_fd_w.is_valid()) {
                std::println("Failed to open file: {}", full_path.string());
                std::terminate();
            }

            int64_t file_size = get_file_size(file_fd_w);
            if (file_size < 0) {
                std::println("Failed to get file size for {}", full_path.string());
                std::terminate();
            }
            file_mmap f(file_size, PROT_READ, MAP_SHARED, file_fd_w, 0);
            file_cache::files.emplace(full_path.string(), std::move(f));
        }
    }
    if (!std::filesystem::exists(env::root_path)) {
        std::println("Root path does not exist: {}", env::root_path.string());
        std::terminate();
    }
    if (!std::filesystem::is_directory(env::root_path)) {
        std::println("Root path is not a directory: {}", env::root_path.string());
        std::terminate();
    }
    return *this;
}

struct app& app::set_addr(std::string_view addr_str) {
    auto parsed = net::parse_addr(addr_str);
    if (parsed.has_value()) {
        env::addr = parsed.value();
    } else {
        std::println("Failed to parse address: {}", parsed.error());
        std::terminate();
    }
    env::fd_w = setup_socket(env::addr);

    if (!env::fd_w.is_valid()){
        std::println("Failed to create socket");
        std::terminate();
    }
    return *this;
}

void app::run(){
    server_loop();
    std::signal(SIGINT, [](int) {
        std::println("Received SIGINT, stopping server...");
        coro_io_ctx::get_instance().request_stop();
    });
    coro_io_ctx::get_instance().run();
}


struct app& app(){
    static struct app instance;
    return instance;
}