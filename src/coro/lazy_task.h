#pragma once
#include <coroutine>
#include <thread>
#include <utility>

namespace coro{
template <typename return_t>
class lazy_task{
public:
    struct promise_type{
        auto get_return_object(){
            return lazy_task{this};
        }

        auto initial_suspend(){
            return std::suspend_never{};
        }

        auto final_suspend() noexcept{
            return std::suspend_always{};
        }
        void unhandled_exception() {  }


        void return_value(const return_t& v){
            value = v;
        }

        void return_value(return_t&& v){
            value = std::move(v);
        }

        return_t value;
    };
    explicit lazy_task(promise_type* p): handle{handle_type::from_promise(*p)}{}
    lazy_task(lazy_task&& other) = delete;
    lazy_task(const lazy_task& other) = delete;
    lazy_task& operator=(lazy_task&& other) = delete;
    lazy_task& operator=(const lazy_task& other) = delete;

    ~lazy_task(){
        while (!handle.done())
            std::this_thread::yield();
        handle.destroy();
    }

    bool done() const{
        return handle.done();
    }

    return_t& get(){
        while (!handle.done()){
            std::this_thread::yield();
        }
        return handle.promise().value;
    }

    return_t&& get_as_rvalue(){
        while (!handle.done()){
            std::this_thread::yield();
        }
        return std::move(handle.promise().value);
    }
private:
    using handle_type = std::coroutine_handle<promise_type>;
    handle_type handle;
};

// final_suspend suspend_never , so automatically destroyed
template <>
class lazy_task<void>{
public:
    struct promise_type{
        lazy_task<void> get_return_object(){
            return {};
        }

        auto initial_suspend(){
            return std::suspend_never{};
        }

        auto final_suspend() noexcept {
            return std::suspend_never{};
        }
        void unhandled_exception() {  }

        void return_void(){}
    };
};

}
