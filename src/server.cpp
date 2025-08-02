#include "server.h"


#include <asm-generic/socket.h>
#include <atomic>
#include <cerrno>
#include <coroutine>
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
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <filesystem>
#include <csignal>
#include <utility>
#include <vector>

#include "coro/lazy_task.h"
#include "coro/task.h"
#include "io.h"
#include "log.h"
#include "coro_io.h"
#include "meta.h"
#include "net/ipv4.h"

using std::literals::operator""s;
using std::literals::operator""ms;

using namespace seele;

namespace web {

namespace env {
    static std::filesystem::path root_path = std::filesystem::current_path() / "www";
    static net::ipv4 addr;
    static std::vector<fd_wrapper> accepter_fd_list;
    static std::unordered_map<std::string, mmap_wrapper> file_caches{};

    std::expected<iovec, http::status_code> get_file_cache(const std::filesystem::path& path) {
        auto full_path = std::filesystem::absolute(path);
        if (auto it = file_caches.find(path.string());it != file_caches.end()) {
            return iovec{
                it->second.data,
                it->second.size
            };
        }
        return std::unexpected{http::status_code::not_found};
    }



    struct GET_handler {
        void* helper_ptr;
        auto (*handler)(void*, const http::query_t&, const http::header_t&) -> handler_response;

        inline handler_response operator()(const http::query_t& query, const http::header_t& header) {
            return handler(helper_ptr, query, header);
        }
    };

    struct POST_handler {
        void* helper_ptr;
        auto (*handler)(void*, const http::query_t&, const http::header_t&, const http::body_t&) -> handler_response;

        inline handler_response operator()(const http::query_t& query, const http::header_t& header, const http::body_t& body) {
            return handler(helper_ptr, query, header, body);
        }
    };


    static std::unordered_map<std::string, GET_handler> get_routings;
    static std::unordered_map<std::string, POST_handler> post_routings;
}

struct wait_promise_init{
    send_task::promise_type* promise;
    auto await_ready() { return false;}
    
    void await_suspend(std::coroutine_handle<send_task::promise_type> handle) {
        this->promise = &handle.promise();
    }
    auto await_resume() {return promise;}
};


send_task send_http_error(http::status_code code){
    auto content = http::error_contents[code];
    http_file_ctx ctx = http_file_ctx::make(
        {
            code,
            {
                {"Content-Type", "text/html; charset=utf-8"},
                {"X-Content-Type-Options", "nosniff"},
            }
        },
        (void*) content.data(),
        content.size()
    );
    auto promise = co_await wait_promise_init{};
    auto fd = promise->fd;
    co_await coro_io::awaiter::link_timeout{
        coro_io::awaiter::writev{fd, &ctx.header, 2},
        200ms
    };

    co_return -1;
}

send_task send_file(http_file_ctx&& ctx){
    auto file_ctx = std::move(ctx);

    auto promise = co_await wait_promise_init{};

    auto fd = promise->fd;
    auto client_addr = promise->client_addr;
    auto timeout = promise->timeout;



    uint32_t total_size = file_ctx.size();
    uint32_t sent_size = 0;
    int32_t res = co_await coro_io::awaiter::link_timeout{
        coro_io::awaiter::writev{
            fd,
            &file_ctx.header,
            2
        },
        timeout
    };

    if (res <= 0) {
        log::async().error(
            "Failed to send response header for {} : {}", 
            client_addr.toString(), coro_io::error::msg
        );
        co_return -1;
    }
    sent_size += res;

    while (sent_size < total_size) {
        auto offset = file_ctx.offset_of(sent_size);
        res = co_await coro_io::awaiter::link_timeout{
            coro_io::awaiter::writev{
                fd,
                &offset,
                1
            },
            timeout
        };
        if (res <= 0) {
            log::async().error(
                "Failed to send response header for {} : {}", 
                client_addr.toString(), coro_io::error::msg
            );
            co_return -1;
        }
        sent_size += res;
    }
    co_return sent_size;
}

send_task send_msg(const http::res_msg& msg){
    
    std::string str = msg.to_string();

    auto promise = co_await wait_promise_init{};

    auto fd = promise->fd;
    auto client_addr = promise->client_addr;
    auto timeout = promise->timeout;

    
    uint32_t total_size = static_cast<uint32_t>(str.size());
    uint32_t sent_size = 0;
    while (sent_size < total_size) {
        auto offset = str.data() + sent_size;
        auto remaining_size = total_size - sent_size;
        int32_t res = co_await coro_io::awaiter::link_timeout{
            coro_io::awaiter::write{
                fd,
                offset,
                remaining_size
            },
            timeout
        };
        if (res <= 0) {
            log::async().error(
                "Failed to send response header for {} : {}", 
                client_addr.toString(), coro_io::error::msg
            );
            co_return -1;
        }
        sent_size += res;
    }
    co_return sent_size;
}



handler_response handle_file_get(const http::req_msg& req){
    auto origin = std::get_if<http::origin_form>(&req.line.target);
    if (!origin) {
        return send_http_error(http::status_code::not_implemented);
    }

    std::filesystem::path origin_path = origin->path;
    std::filesystem::path full_path = env::root_path / origin_path.relative_path();

    std::filesystem::path rel = full_path.lexically_relative(env::root_path);

    if (rel.empty() || *rel.begin() == ".."){
        log::async().error("Attempted to access outside of root path: {}", full_path.string());
        return send_http_error(http::status_code::forbidden);
    }

    if (std::filesystem::is_directory(full_path)) {
        full_path /= "index.html";
    }

    if(auto ctx = env::get_file_cache(full_path);!ctx.has_value()){
        return send_http_error(ctx.error());
    } else {

        std::string content_type = "application/octet-stream";
        std::string ext = full_path.extension().string();

        if (auto it = http::mime_types.find(ext);it != http::mime_types.end()) {
            content_type = it->second;
        }

        return send_file(
            http_file_ctx::make(
                {
                    http::status_code::ok,
                    {
                        {"Content-Type", content_type},
                        {"X-Content-Type-Options", "nosniff"},
                    }
                }, 
                ctx.value().iov_base, 
                ctx.value().iov_len
            )
        );
    }

};



handler_response handle_req(const http::req_msg& req){
    switch (req.line.method) {
        case http::method_t::GET: {
            if (auto origin = std::get_if<http::origin_form>(&req.line.target)) {
                if(auto it = env::get_routings.find(origin->path); it != env::get_routings.end()) {
                    return it->second(origin->query, req.header);
                } else {
                    return handle_file_get(req);
                }
            }
            return send_http_error(http::status_code::not_implemented);
        }
        default:
            return send_http_error(http::status_code::not_implemented);
    
    }
}




coro::task async_handle_connection(int fd, net::ipv4 addr) {
    fd_wrapper fd_w(fd);
    net::ipv4 client_addr = addr;
    co_await coro::thread::dispatch_awaiter{};

    char read_buffer[8192];
    std::string_view buffer_view;
    auto timeout = 500ms;
    while (true) {
        http::req_msg msg{};
        auto parser = msg.parser();

        if (!buffer_view.empty()) {
            parser.send_and_resume(buffer_view);
        }
        while (!parser.done()) {
            int32_t res = co_await coro_io::awaiter::link_timeout{
                coro_io::awaiter::read{fd_w.get(), read_buffer, sizeof(read_buffer)},
                timeout
            };

            if (auto bytes_read = res; bytes_read <= 0) {
                log::async().error("Failed to read from {}: {}", 
                    client_addr.toString(), coro_io::error::msg
                );
                co_return;
            } else {
                parser.send_and_resume({read_buffer, static_cast<size_t>(bytes_read)});
            }
        }

        if (auto result = parser.get(); result.has_value()) {

            buffer_view = result.value();

            if (auto it = msg.header.find("Connection"); it != msg.header.end()) {
                if (it->second == "close") {
                    co_return; // Close connection immediately
                } else if (it->second == "keep-alive") {
                    timeout = 10000ms; // Keep-alive timeout
                }
            }

            if (co_await handle_req(msg).await(fd_w.get(), client_addr, timeout) < 0) {
                co_return;
            }

        } else {
            log::async().error("Failed to parse request from {}", client_addr.toString());
            co_await send_http_error(http::status_code::bad_request).await(fd_w.get(), client_addr, timeout);
            co_return;
        }

    }
}



coro::task server_loop(int32_t _fd) {
    int32_t fd = _fd;
    co_await coro::thread::dispatch_awaiter{};
    while(true){
        sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

        int32_t ret = co_await 
            coro_io::awaiter::accept{fd, (sockaddr *)&client_addr, &client_addr_len};
        switch (ret) {
            case coro_io::error::SYS:
            case coro_io::error::CTX_CLOSED:
                log::async().error("Failed to accept connection: {}", coro_io::error::msg);
                co_return;
            case coro_io::error::TIMEOUT:
                log::async().debug("Accept timed out, retrying...");
                continue;
            default:
                break;
        
        }
        auto ipv4_addr = net::ipv4::from_sockaddr_in(client_addr);
        log::async().info("Fd[{}]: Accepted connection from {}", fd, ipv4_addr.toString());
        async_handle_connection(ret, ipv4_addr);
    }

    co_return;
}

coro::task cancel(int32_t fd) {
    if (co_await coro_io::awaiter::cancel_fd{fd} != 0){
        std::println("Failed to cancel fd: {}", fd);
    }
}
} // namespace web



struct app& app::set_root_path(std::string_view path) {
    web::env::root_path = std::filesystem::absolute(path);

    for (const auto& entry : std::filesystem::recursive_directory_iterator(web::env::root_path)) {
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
            web::env::file_caches.emplace(full_path.string(), mmap_wrapper(file_size, PROT_READ, MAP_SHARED, file_fd_w.get(), 0));
        }
    }
    if (!std::filesystem::exists(web::env::root_path)) {
        std::println("Root path does not exist: {}", web::env::root_path.string());
        std::terminate();
    }
    if (!std::filesystem::is_directory(web::env::root_path)) {
        std::println("Root path is not a directory: {}", web::env::root_path.string());
        std::terminate();
    }
    return *this;
}

struct app& app::set_addr(std::string_view addr_str) {
    auto parsed = net::parse_addr(addr_str);
    if (parsed.has_value()) {
        web::env::addr = parsed.value();
    } else {
        std::println("Failed to parse address: {}", parsed.error());
        std::terminate();
    }
    return *this;
}

struct app& app::GET(std::string_view path, void* helper_ptr, auto (*handler)(void*, const http::query_t&, const http::header_t&) -> web::handler_response) {
    web::env::get_routings.emplace(std::string(path), web::env::GET_handler{helper_ptr, handler});
    return *this;
}

struct app& app::POST(std::string_view path, void* helper_ptr, auto (*handler)(void*, const http::query_t&, const http::header_t&, const http::body_t&) -> web::handler_response){
    web::env::post_routings.emplace(std::string(path), web::env::POST_handler{helper_ptr, handler});
    return *this;
}


void app::run(){
    if (!web::env::addr.is_valid()){
        std::println("Server address is not valid");
        std::terminate();
    }
    constexpr size_t accepter_count = 8;
    constexpr size_t max_accepter_connections = 256;
    web::env::accepter_fd_list.reserve(accepter_count);
    for (size_t i = 0; i < accepter_count; ++i) {
        auto fd_w = setup_socket(web::env::addr, max_accepter_connections, [](int fd) {
            int opt = 1;
            setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
        });
        if (!fd_w.is_valid()) {
            std::println("Failed to setup socket: {}", strerror(errno));
            std::terminate();
        }
        web::env::accepter_fd_list.push_back(std::move(fd_w));
    }

    for (uint32_t i = 0; i < web::env::accepter_fd_list.size(); ++i) {
        web::server_loop(web::env::accepter_fd_list[i].get());
    }    

    std::signal(SIGINT, [](int) {
        std::println("Received SIGINT, stopping server...");
        for (auto& fd : web::env::accepter_fd_list) {
            web::cancel(fd.get());
        }
        coro_io::ctx::get_instance().request_stop(); 
    });
    coro_io::ctx::get_instance().run();
}


struct app& app(){
    static struct app instance;
    return instance;
}