#pragma once
#include <bit>
#include <cstddef>
#include <type_traits>
#include <functional>

namespace seele {

template <typename signature_t>
class function_ref {
    static_assert(false, "signature_t must be a function type");
};

template <typename R, typename... args_t>
class function_ref<R(args_t...)> {
public:
    using signature_t = R(args_t...);
    
    using erased_t = union {
        void* ctx;
        signature_t* fn;
    };

    function_ref(const function_ref&) = default;
    function_ref(function_ref&&) = default;

    function_ref& operator=(const function_ref&) = default;
    function_ref& operator=(function_ref&&) = default;


    template<typename invocable_t>
        requires std::is_invocable_r_v<R, invocable_t, args_t...> && (!std::is_convertible_v<invocable_t, signature_t*>)
                && (!std::is_same_v<function_ref<R(args_t...)>, invocable_t>)
    constexpr function_ref(invocable_t& inv)
      : proxy{[](erased_t c, args_t... args) -> R {
            return std::invoke(*static_cast<invocable_t*>(c.ctx), static_cast<args_t>(args)...);
        }},
        ctx{.ctx = static_cast<void*>(&inv)}
    {}

    template<typename invocable_t>
        requires std::is_invocable_r_v<R, invocable_t, args_t...> && std::is_convertible_v<invocable_t, signature_t*>
    constexpr function_ref(const invocable_t& inv)
      : proxy{[](erased_t c, args_t... args) -> R {
            return std::invoke(c.fn, static_cast<args_t>(args)...);
        }},
        ctx{.fn = inv}
    {}

    constexpr R operator()(args_t... args) const { return proxy(ctx, static_cast<args_t>(args)...); }

private:
    R (*proxy)(erased_t, args_t...);
    erased_t ctx;
};



} // namespace seele
