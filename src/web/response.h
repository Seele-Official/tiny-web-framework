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
                {"Content-Length", "0"},
                {"Connection", "close"},
            }
        };
        std::vector<char> buffer{};
        buffer.reserve(256);
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

inline task file(const std::string& content_type, std::span<std::byte> content){
    return [](const std::string& content_type, std::span<std::byte> content) -> detail::send_task {
        std::vector<char> header{};
        header.reserve(256);

        http::response::msg msg {
            {http::response::status_code::ok},
            {
                {"Content-Type", content_type},
                {"Content-Length", std::to_string(content.size())},
                {"Connection", "keep-alive"},
            }
        };

        msg.format_to(std::back_inserter(header));

        auto [fd, client_addr, timeout] = co_await task::wait_setting{};


        iovec iov[2] = {
            { header.data(), header.size() },
            { content.data(), content.size() }
        };

        size_t total_size = header.size() + content.size();
        size_t sent_size = 0;


        struct iovec* current_iov = iov;
        uint32_t iov_count = 2;

        while (sent_size < total_size) {
            int32_t res = co_await io::awaiter::link_timeout{
                io::awaiter::writev{ fd, current_iov, iov_count },
                timeout
            };

            if (res <= 0) {
                log::async::error(
                    "Failed to send response for {} : {}", 
                    client_addr.to_string(), io::error::msg
                );
                co_return -1;
            }

            sent_size += res;
            auto written_bytes = static_cast<size_t>(res);

            while (written_bytes > 0 && iov_count > 0) {
                if (written_bytes < current_iov->iov_len) {
                    current_iov->iov_base = static_cast<char*>(current_iov->iov_base) + written_bytes;
                    current_iov->iov_len -= written_bytes;
                    written_bytes = 0; 
                } else {
                    written_bytes -= current_iov->iov_len;
                    current_iov++;
                    iov_count--;
                }
            }
        }


        co_return sent_size;
    }(content_type, content);
}

inline task file(const std::string& content_type, const io::mmap& content){
    return response::file(content_type, {content.get_data(), content.get_size()});
}



inline task msg(const http::response::msg& msg){ 
    return [](const http::response::msg& msg) -> detail::send_task {
        std::vector<char> buffer{};

        buffer.reserve(1024 + msg.body.size());

        msg.format_to(std::back_inserter(buffer));

        auto [fd, client_addr, timeout] = co_await task::wait_setting{};

        
        size_t total_size = buffer.size();
        size_t sent_size = 0;
        while (sent_size < total_size) {
            auto offset = buffer.data() + sent_size;
            auto remaining_size = total_size - sent_size;
            int32_t res = co_await io::awaiter::link_timeout{
                io::awaiter::write{
                    fd,
                    offset,
                    (uint32_t) remaining_size
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

