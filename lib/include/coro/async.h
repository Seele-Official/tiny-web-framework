#pragma once
#include <concepts>
#include <tuple>
#include <utility>
#include <type_traits>
#include "threadpool.h"
#include "lazy_task.h"
namespace seele::coro{

template <typename lambda_t, typename... args_t>
    requires std::invocable<lambda_t, std::decay_t<args_t>&...>
lazy_task<std::invoke_result_t<lambda_t, std::decay_t<args_t>&...>> 
    async(lambda_t&& lambda, args_t&&... args){ 
        auto l = std::forward<lambda_t>(lambda);
        std::tuple<std::decay_t<args_t>...> t{std::forward<args_t>(args)...};

        co_await coro::thread::dispatch_awaiter{};
        co_return std::apply(l, t);
    }
}