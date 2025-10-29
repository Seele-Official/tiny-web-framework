#pragma once

#include <concepts>
#include <coroutine>
#include <chrono>
#include <cstdint>

#include "io/io.h"
#include "web/ip.h"
#include "http/response.h"

namespace web::response {


struct task {
public:
    struct settings{
        int32_t                     fd{};
        web::ip::v4                 client_addr{};
        std::chrono::milliseconds   timeout{};
    };

    struct promise_type{
        task get_return_object(){
            return this;
        }

        auto initial_suspend(){
            return std::suspend_always{};
        }

        struct final_awaiter {
            bool await_ready() noexcept { return false; }
            void await_resume() noexcept {}
            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept {
                return h.promise().previous;
            }
        };
        
        auto final_suspend() noexcept {
            return final_awaiter{};
        }
        void unhandled_exception() noexcept {}

        void return_value(int64_t v) noexcept {
            ret = v;
        }

        int64_t                     ret{};
        std::coroutine_handle<>     previous{std::noop_coroutine()};

        settings                   sets{}; 
    };

    struct get_settings{
        auto await_ready() { return false;}

        bool await_suspend(std::coroutine_handle<promise_type> handle) {
            this->promise = &handle.promise();
            return false;
        }
        auto await_resume() {return promise->sets;}

        promise_type* promise;
    };    
    
    struct awaiter{
        bool await_ready() { return false; }
        int64_t await_resume() { return std::move(this->coro.promise().ret); }

        auto await_suspend(std::coroutine_handle<> h) {
            coro.promise().previous = h;
            return coro;
        }
        template<typename T>
            requires std::derived_from<T, promise_type>
        auto await_suspend(std::coroutine_handle<T> h) {
            coro.promise().sets = h.promise().sets;
            coro.promise().previous = h;
            return coro;
        }
        std::coroutine_handle<promise_type> coro;
    };

    task(promise_type* p): handle{handle_type::from_promise(*p)}{}
    task(task& other) = delete;
    task(task&& other) noexcept {
        if (this != &other){
            this->handle = std::move(other.handle);
            other.handle = nullptr;             
        }
    }
    task& operator=(task& other) = delete;
    task& operator=(task&& other) noexcept {
        if (this != &other) {
            this->handle = std::move(other.handle);
            other.handle = nullptr;
        }
        return *this;
    }

    ~task(){
        if (this->handle){
            this->handle.destroy();
        }
    }    
    
    task& settings(settings sets){
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


task msg(const http::response::msg& msg);

task error(http::response::status_code code);

task file_head(const std::string& content_type, size_t size);

task file(const std::string& content_type, std::span<std::byte> content);

task file(const std::string& content_type, const io::mmap& content);
};

