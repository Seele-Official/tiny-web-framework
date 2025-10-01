#pragma once
#include <bits/types/struct_iovec.h>
#include <coroutine>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "io/awaiter.h"
#include "log.h"

#include "io/io.h"
#include "web/ip.h"

#include "http/response.h"



namespace web::response {
using namespace seele;
namespace detail {

struct settings{
    int32_t                     fd{};
    web::ip::v4                 client_addr{};
    std::chrono::milliseconds   timeout{};
};

struct send_task {
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

        int64_t                     ret{};
        std::coroutine_handle<>     previous{std::noop_coroutine()};

        settings                   sets{}; 
    };

    send_task(promise_type* p): handle{handle_type::from_promise(*p)}{}
    send_task(send_task& other) = delete;
    send_task(send_task&& other) noexcept {
        if (this != &other){
            this->handle = std::move(other.handle);
            other.handle = nullptr;             
        }
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

    send_task& init(settings sets){
        this->handle.promise().sets = sets;
        return *this;
    }

    awaiter operator co_await(){
        return awaiter{this->handle};
    }

private:
    using handle_type = std::coroutine_handle<promise_type>;
    handle_type handle;
};

}

struct task {
    struct wait_setting{
        auto await_ready() { return false;}
        
        void await_suspend(std::coroutine_handle<detail::send_task::promise_type> handle) {
            this->promise = &handle.promise();
        }
        auto await_resume() {return promise->sets;}

        detail::send_task::promise_type* promise;
    };

    auto& settings(int fd, web::ip::v4 client_addr, std::chrono::milliseconds timeout){
        return stask.init({fd, client_addr, timeout});
    }

    task() = delete;
    task(const task&) = delete;
    task(task&&) = default;
    ~task() = default;
    task(detail::send_task&& stask) : stask(std::move(stask)){}

    detail::send_task stask;
};

inline task error(http::response::status_code code){
    return [](http::response::status_code code) -> detail::send_task {
        http::response::msg msg{
            {code},
            {
                {"Content-Type", "text/html; charset=utf-8"},
                {"X-Content-Type-Options", "nosniff"},
            }
        };
        std::vector<char> buffer{};
        buffer.reserve(128);
        msg.format_to(std::back_inserter(buffer));
        // TODO: Add a simple HTML body based on status code
        // For now, we just send the status line and headers
        auto [fd, client_addr, timeout] = co_await task::wait_setting{};

        co_return co_await io::awaiter::link_timeout{
            io::awaiter::write{fd, buffer.data(), (uint32_t) buffer.size()},
            timeout
        };

    }(code);
}

inline task file(const std::string& content_type, size_t size, void* data){
    return [](const std::string& content_type, size_t size, void* data) -> detail::send_task {
        std::vector<char> header_buffer{};
        header_buffer.reserve(256);

        http::response::msg msg{
            {http::response::status_code::ok},
            {
                {"Content-Type", content_type},
                {"Content-Length", std::to_string(size)},
                {"Connection", "keep-alive"},
            }
        };

        msg.format_to(std::back_inserter(header_buffer));

        iovec iov[2] = {
            {
                header_buffer.data(),
                header_buffer.size()
            },
            {
                data,
                size
            }
        };

        auto [fd, client_addr, timeout] = co_await task::wait_setting{};


        uint32_t total_size = iov[0].iov_len + iov[1].iov_len;
        uint32_t sent_size = 0;
        int32_t res = co_await io::awaiter::link_timeout{
            io::awaiter::writev{
                fd,
                iov,
                2
            },
            timeout
        };

        if (res <= 0) {
            log::async::error(
                "Failed to send response header for {} : {}", 
                client_addr.to_string(), io::error::msg
            );
            co_return -1;
        }
        sent_size += res;

        while (sent_size < total_size) {
            auto offset = [&]() -> iovec {
                if (sent_size < iov[0].iov_len) [[unlikely]] {
                    return {static_cast<std::byte*>(iov[0].iov_base) + sent_size, iov[0].iov_len + iov[0].iov_len - sent_size};
                } else {
                    auto offset_in_data = sent_size - iov[0].iov_len;
                    return {static_cast<std::byte*>(iov[1].iov_base) + offset_in_data, iov[1].iov_len - offset_in_data};
                }
            }();

            res = co_await io::awaiter::link_timeout{
                io::awaiter::writev{
                    fd,
                    &offset,
                    1
                },
                timeout
            };

            if (res <= 0) {
                log::async::error(
                    "Failed to send response header for {} : {}", 
                    client_addr.to_string(), io::error::msg
                );
                co_return -1;
            }
            sent_size += res;
        }

        co_return sent_size;
    }(content_type, size, data);
}

inline task file(const std::string& content_type, const io::mmap& content){
    return response::file(content_type, content.get_size(), content.get_data());
}



inline task msg(const http::response::msg& msg){ 
    return [](const http::response::msg& msg) -> detail::send_task {
        std::vector<char> buffer{};

        buffer.reserve(1024 + msg.body.size());

        msg.format_to(std::back_inserter(buffer));

        auto [fd, client_addr, timeout] = co_await task::wait_setting{};

        
        uint32_t total_size = static_cast<uint32_t>(buffer.size());
        uint32_t sent_size = 0;
        while (sent_size < total_size) {
            auto offset = buffer.data() + sent_size;
            auto remaining_size = total_size - sent_size;
            int32_t res = co_await io::awaiter::link_timeout{
                io::awaiter::write{
                    fd,
                    offset,
                    remaining_size
                },
                timeout
            };
            if (res <= 0) {
                log::async::error(
                    "Failed to send response for `{}`: `{}`", 
                    client_addr.to_string(), io::error::msg
                );
                co_return -1;
            }
            sent_size += res;
        }
        co_return sent_size;
    }(msg);

}
};

