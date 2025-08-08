#pragma once
#include <bits/types/struct_iovec.h>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <variant>
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

struct handler_response{
    send_task task;
    auto await(int fd, seele::net::ipv4 client_addr, std::chrono::milliseconds timeout){
        return task.await(fd, client_addr, timeout);
    }
    handler_response(send_task&& t): task(std::move(t)){}
};

send_task send_http_error(http::status_code code);
send_task send_file(http_file_ctx&& ctx);
send_task send_msg(const http::res_msg& msg);

} // namespace web



struct app{
    app& set_root_path(std::string_view path);

    app& set_addr(std::string_view addr_str);

    template<typename invocable_t>
        requires std::is_invocable_r_v<web::handler_response, invocable_t, const http::query_t&, const http::header_t&>
    app& GET(std::string_view path, invocable_t& handler);

    app& GET(std::string_view path, void* helper_ptr, auto (*handler)(void*, const http::query_t&, const http::header_t&) -> web::handler_response);

    template<typename invocable_t>
        requires std::is_invocable_r_v<web::handler_response, invocable_t, const http::query_t&, const http::header_t&, const http::body_t&>
    app& POST(std::string_view path, invocable_t& handler);

    app& POST(std::string_view path, void* helper_ptr, auto (*handler)(void*, const http::query_t&, const http::header_t&, const http::body_t&) -> web::handler_response);

    void run();
};

template<typename invocable_t>
    requires std::is_invocable_r_v<web::handler_response, invocable_t, const http::query_t&, const http::header_t&> 
app& app::GET(std::string_view path, invocable_t& handler){
    static_assert(!std::is_function_v<invocable_t>, "Handler cannot be a function, use a lambda or a functor instead.");

    return GET(path, &handler, [](void* helper_ptr, const http::query_t& query, const http::header_t& header) -> web::handler_response {
        return static_cast<invocable_t*>(helper_ptr)->operator()(query, header);
    });

}

template<typename invocable_t>
    requires std::is_invocable_r_v<web::handler_response, invocable_t, const http::query_t&, const http::header_t&, const http::body_t&>
app& app::POST(std::string_view path, invocable_t& handler){
    static_assert(!std::is_function_v<invocable_t>, "Handler cannot be a function, use a lambda or a functor instead.");

    return POST(path, &handler, [](void* helper_ptr, const http::query_t& query, const http::header_t& header, const http::body_t& body) -> web::handler_response {
        return static_cast<invocable_t*>(helper_ptr)->operator()(query, header, body);
    });

}



app& app();

