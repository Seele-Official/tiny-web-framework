#pragma once
#include <coroutine>
#include <thread>
#include <utility>

namespace coro{
template<typename return_t>
class awaitable_task{
public:
    struct promise_type{
        awaitable_task get_return_object(){
            return this;
        }

        auto initial_suspend(){
            return std::suspend_always{};
        }

        struct final_awaiter {
            bool await_ready(){ return false; }
            void await_resume(){}
            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h){
                return h.promise().previous;
            }
        };
        auto final_suspend() noexcept {
            return final_awaiter{};
        }
        void unhandled_exception(){}

        void return_value(const return_t& v){
            result = v;
        }

        void return_value(return_t&& v){
            result = std::move(v);
        }
        std::coroutine_handle<> previous{std::noop_coroutine()};
        return_t result;
    };

    struct awaiter{
        bool await_ready() { return false; }
        return_t await_resume() { return std::move(coro.promise().result); }
        auto await_suspend(std::coroutine_handle<> h)
        {
            coro.promise().previous = h;
            return coro;
        }
        std::coroutine_handle<promise_type> coro;
    };
    awaiter operator co_await() { return this->handle; }
    
    awaitable_task(promise_type* p): handle{handle_type::from_promise(*p)}{}
    ~awaitable_task(){
        this->handle.destroy();
    }
private:
    using handle_type = std::coroutine_handle<promise_type>;
    handle_type handle;
};

}