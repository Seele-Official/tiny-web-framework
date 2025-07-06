
#pragma once
#include <coroutine>
#include <thread>
#include <utility>


namespace seele::coro{
    template <typename return_t, typename message_t>
    class co_task{
    public:
        struct promise_type{
            return_t return_v;
            message_t* yield_v;
            auto get_return_object(){
                return co_task{this};
            }

            auto initial_suspend(){
                return std::suspend_never{};
            }

            auto final_suspend() noexcept{
                return std::suspend_always{};
            }
            void unhandled_exception() {  }


            void return_value(const return_t& v){
                return_v = v;
            }

            void return_value(return_t&& v){
                return_v = std::move(v);
            }

            auto yield_value(message_t& v){
                yield_v = &v;
                return std::suspend_always{};
            }
        };
    private:
        using handle_type = std::coroutine_handle<promise_type>;

        handle_type handle;


    public:

        explicit co_task(promise_type* p): handle{handle_type::from_promise(*p)}{}
        co_task(co_task&& other) = delete;
        co_task(const co_task& other) = delete;
        co_task& operator=(co_task&& other) = delete;
        co_task& operator=(const co_task& other) = delete;

        ~co_task(){
            handle.destroy();
        }

        bool done() const{
            return handle.done();
        }

        void send_and_resume(const message_t& v){
            if (!handle.done()){
                *handle.promise().yield_v = v;
                handle.resume();
            }
        }


        return_t& get(){
            while (!handle.done()){
                std::this_thread::yield();
            }
            return handle.promise().return_v;
        }

        return_t&& get_as_rvalue(){
            while (!handle.done()){
                std::this_thread::yield();
            }
            return std::move(handle.promise().return_v);
        }

    };
}

#define co_wait_message co_yield