#pragma once

#include <coroutine>
#include <chrono>
#include <cstdint>

#include "io/io.h"
#include "web/ip.h"
#include "http/response.h"

namespace web::response {
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
} // namespace detail

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

task msg(const http::response::msg& msg);

task error(http::response::status_code code);

task file_head(const std::string& content_type, size_t size);

task file(const std::string& content_type, std::span<std::byte> content);

task file(const std::string& content_type, const io::mmap& content);
};

