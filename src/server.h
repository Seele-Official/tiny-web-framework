#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include "http.h"
#include "io.h"
#include "meta.h"

namespace web {
struct http_file_ctx{
    iovec_wrapper header;
    iovec data;
    static http_file_ctx make(http::res_msg& msg, void* file, size_t size) {
        http_file_ctx res{
            {256},
            {file, size}
        };
        msg.set_content_length(size);
        auto it = msg.format_to(static_cast<char*>(res.header.iov_base));
        res.header.iov_len = seele::meta::safe_cast<size_t>(it - static_cast<char*>(res.header.iov_base));
        return res;
    }
    static http_file_ctx make(http::res_msg&& msg, void* file, size_t size) {
        http_file_ctx res{
            {256},
            {file, size}
        };
        msg.set_content_length(size);
        auto it = msg.format_to(static_cast<char*>(res.header.iov_base));
        res.header.iov_len = seele::meta::safe_cast<size_t>(it - static_cast<char*>(res.header.iov_base));
        return res;
    }

    uint32_t size() const {
        return static_cast<uint32_t>(header.iov_len + data.iov_len);
    }
    iovec offset_of(size_t offset) {
        if (offset < header.iov_len) {
            return {static_cast<std::byte*>(header.iov_base) + offset, data.iov_len + header.iov_len - offset};
        } else {
            auto offset_in_data = offset - header.iov_len;
            return {static_cast<std::byte*>(data.iov_base) + offset_in_data, data.iov_len - offset_in_data};
        }
    }

};

struct send_task{
public:
    struct promise_type{
        send_task get_return_object(){
            return this;
        }

        auto initial_suspend(){
            return std::suspend_never{};
        }

        struct final_awaiter {
            bool await_ready() noexcept { return false; }
            void await_resume() noexcept {}
            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h){
                return h.promise().previous;
            }
        };
        auto final_suspend() noexcept {
            return final_awaiter{};
        }
        void unhandled_exception(){}

        void return_value(int64_t v){
            ret = v;
        }
        int32_t fd;
        int64_t ret;
        seele::net::ipv4 client_addr;
        std::chrono::milliseconds timeout;
        std::coroutine_handle<> previous;
    };

    send_task(promise_type* p): handle{handle_type::from_promise(*p)}{}
    send_task(send_task& other) = delete;
    send_task(send_task&& other) noexcept{
        this->handle = std::move(other.handle);
        other.handle = nullptr; 
    }
    send_task& operator=(send_task& other) = delete;
    send_task& operator=(send_task&& other) noexcept {
        if (this != &other) {
            this->handle = std::move(other.handle);
            other.handle = nullptr;
        }
        return *this;
    }

    ~send_task(){
        if (this->handle){
            this->handle.destroy();
        }
    }    
    
    struct awaiter{
        bool await_ready() { return false; }
        int64_t await_resume() { return std::move(this->coro.promise().ret); }

        auto await_suspend(std::coroutine_handle<> h)
        {
            coro.promise().previous = h;
            return coro;
        }
        std::coroutine_handle<promise_type> coro;
    };

    awaiter await(int fd, seele::net::ipv4 client_addr, std::chrono::milliseconds timeout){
        this->handle.promise().fd = fd;
        this->handle.promise().client_addr = client_addr;
        this->handle.promise().timeout = timeout;
        return awaiter{this->handle};
    }
private:
    using handle_type = std::coroutine_handle<promise_type>;
    handle_type handle;
};

struct task{
    send_task t;
    auto await(int fd, seele::net::ipv4 client_addr, std::chrono::milliseconds timeout){
        return t.await(fd, client_addr, timeout);
    }
    task(send_task&& t): t(std::move(t)){}
};

task send_http_error(http::status_code code);
task send_file(http_file_ctx&& ctx);
task send_msg(const http::res_msg& msg);

} // namespace web

using GET_route_handler_t = seele::meta::function_ref<web::task(const http::query_t&, const http::header_t&)>;
using POST_route_handler_t = seele::meta::function_ref<web::task(const http::query_t&, const http::header_t&, const http::body_t&)>;

struct app{
    app& set_root_path(std::string_view path);

    app& set_addr(std::string_view addr_str);

    app& GET(std::string_view path, GET_route_handler_t handler);

    app& POST(std::string_view path, POST_route_handler_t handler);
    
    void run();
};



app& app();

