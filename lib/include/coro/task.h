#pragma once
#include <coroutine>
#include <thread>
#include <utility>

namespace seele::coro{
// final_suspend suspend_never , so automatically destroyed
class task{
public:
    struct promise_type{
        task get_return_object(){
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