#include "server.h"


#include <atomic>
#include <cstdint>
#include <cstring>
#include <exception>
#include <expected>
#include <format>
#include <liburing/io_uring.h>
#include <memory>
#include <mutex>
#include <optional>
#include <print>
#include <string>
#include <cstddef>
#include <string_view>
#include <unordered_map>
#include <filesystem>
#include <csignal>

#include "coro/task.h"
#include "log.h"
#include "coro_io.h"
#include "meta.h"
#include "net/ipv4.h"

using std::literals::operator""s;
using std::literals::operator""ms;

using namespace seele;

namespace env {
    static std::filesystem::path root_path = std::filesystem::current_path() / "www";
    static net::ipv4 addr;
    static fd_wrapper fd_w;

    static std::unordered_map<std::string, file_mmap> file_caches{};

    std::expected<iovec, http::error_code> get_file_cache(const std::filesystem::path& path) {
        auto full_path = std::filesystem::absolute(path);
        if (auto it = file_caches.find(path.string());it != file_caches.end()) {
            return iovec{
                it->second.data,
                it->second.size
            };
        }
        return std::unexpected{http::error_code::not_found};
    }



    struct GET_handler {
        void* helper_ptr;
        auto (*handler)(void*, const http::query_t&, const http::header_t&) -> std::expected<handler_response, http::error_code>;

        inline std::expected<handler_response, http::error_code>
        operator()(const http::query_t& query, const http::header_t& header) {
            return handler(helper_ptr, query, header);
        }
    };
    static std::unordered_map<std::string, GET_handler> get_routings;

}



std::expected<handler_response, http::error_code> handle_file_get(const http::req_msg& req) {

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

    if(auto ctx = env::get_file_cache(full_path);!ctx.has_value()){
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
                },
                std::nullopt
            }, 
            ctx.value().iov_base, 
            ctx.value().iov_len
        );
    }
}



std::expected<handler_response, http::error_code> handle_req(const http::req_msg& req, net::ipv4 addr){
    switch (req.line.method) {
        case http::method_t::GET: {
            auto origin = std::get_if<http::origin_form>(&req.line.target);            
            if (origin) {            
                if (auto it = env::get_routings.find(origin->path); it != env::get_routings.end()) {
                    return it->second(origin->query, req.header);
                }
                log::async().info("Handling file GET request from {} for path: {}", addr.toString(), origin->path);
                return handle_file_get(req);
            }
            return std::unexpected{http::error_code::not_implemented};
        }
        break;
        default:{
                return std::unexpected{http::error_code::not_implemented};
        }
    }
}



struct send_http_error {
    http_file_ctx ctx;
    coro_io::awaiter::link_timeout<coro_io::awaiter::writev> awaiter;

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
                },
                std::nullopt
            },
            (void*) content.data(),
            content.size()
        );
        this->awaiter = coro_io::awaiter::link_timeout{
            coro_io::awaiter::writev{fd, &ctx.header, 2},
            200ms
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
    auto timeout = 500ms;
    while (true) {
        auto parser = http::req_msg::parser();
        if (remain.has_value()) {
            parser.send_and_resume(remain.value());
            remain = std::nullopt;
        }
        while (!parser.done()) {       
            std::optional<io_uring_cqe> res = co_await coro_io::awaiter::link_timeout{
                coro_io::awaiter::read{fd_w, read_buffer, sizeof(read_buffer)},
                timeout
            };
            
            if (!res.has_value()) {
                log::async().warn("Read timed out for {}", client_addr.toString());
                co_return;
            }

            auto bytes_read = res.value().res;
            if (bytes_read == 0) {
                log::async().warn("Connection closed by client {}", client_addr.toString());
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

            if (auto it = msg.header.find("Connection"); it != msg.header.end()) {
                if (it->second == "close") {
                    co_return; // Close connection immediately
                } else if (it->second == "keep-alive") {
                    timeout = 10000ms; // Keep-alive timeout
                }
            }


            if (auto handle_res = handle_req(msg, client_addr); handle_res.has_value()) {
                if (auto file_ctx = std::get_if<http_file_ctx>(&handle_res.value()); file_ctx) {

                    uint32_t total_size = file_ctx->size();
                    uint32_t sent_size = 0;                    
                    std::optional<io_uring_cqe> res = co_await coro_io::awaiter::link_timeout{
                        coro_io::awaiter::writev{
                            fd_w,
                            &file_ctx->header,
                            2
                        },
                        timeout
                    };
                    
                    if (!res.has_value() || res.value().res <= 0) {
                        log::async().error("Failed to send response header for {} : {}", 
                                           client_addr.toString(), 
                                           res.has_value() ? strerror(-res.value().res) : "timeout");
                        co_return;
                    }
                    sent_size += res.value().res;

                    while (sent_size < total_size) {
                        auto offset = file_ctx->offset_of(sent_size);
                        res = co_await coro_io::awaiter::link_timeout{
                            coro_io::awaiter::writev{
                                fd_w,
                                &offset,
                                1
                            },
                            timeout
                        };
                        if (!res.has_value() || res.value().res <= 0) {
                            log::async().error("Failed to send file data for {} : {}", 
                                               client_addr.toString(), 
                                               res.has_value() ? strerror(-res.value().res) : "timeout");
                            co_return;
                        }
                        sent_size += res.value().res;
                    }
                } else if (auto msg_res = std::get_if<http::res_msg>(&handle_res.value()); msg_res) {
                    std::string str = msg_res->to_string();
                    uint32_t total_size = static_cast<uint32_t>(str.size());
                    uint32_t sent_size = 0;
                    while (sent_size < total_size) {
                        auto offset = str.data() + sent_size;
                        auto remaining_size = total_size - sent_size;
                        std::optional<io_uring_cqe> res = co_await coro_io::awaiter::link_timeout{
                            coro_io::awaiter::write{
                                fd_w,
                                offset,
                                remaining_size
                            },
                            timeout
                        };

                        if (!res.has_value() || res.value().res <= 0) {
                            log::async().error("Failed to send response header for {} : {}", 
                                            client_addr.toString(), 
                                            res.has_value() ? strerror(-res.value().res) : "timeout");
                            co_return;
                        }
                        sent_size += res.value().res;
                    }

                } else {
                    log::async().error("Unexpected response type");
                    co_return;
                }
            } else {
                co_await send_http_error{fd_w, handle_res.error()};
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

        std::optional<io_uring_cqe> res = co_await coro_io::awaiter::link_timeout{
            coro_io::awaiter::accept{env::fd_w, (sockaddr *)&client_addr, &client_addr_len},
            5s
        };

        if (!res.has_value()){
            log::async().info("Accept timed out");
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
            if (file_size == 0) {
                std::println("File is empty: {}", full_path.string());
                continue;
            }
            file_mmap f(file_size, PROT_READ, MAP_SHARED, file_fd_w, 0);
            env::file_caches.emplace(full_path.string(), std::move(f));
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
    env::fd_w = setup_socket(env::addr, SOMAXCONN);

    if (!env::fd_w.is_valid()){
        std::println("Failed to create socket, error: {}", strerror(errno));
        std::terminate();
    }
    return *this;
}

struct app& app::GET(std::string_view path, void* helper_ptr, auto (*handler)(void*, const http::query_t&, const http::header_t&) -> std::expected<handler_response, http::error_code>) {
    env::get_routings.emplace(std::string(path), env::GET_handler{helper_ptr, handler});
    return *this;
}


void app::run(){
    if (!env::addr.is_valid()){
        std::println("Server address is not valid");
        std::terminate();
    }
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