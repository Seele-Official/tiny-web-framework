#pragma once
#include <concepts>
#include <optional>
#include <type_traits>
#include <tuple>
#include <variant>
#include <utility>
#include <cstddef>
#include <ranges>
#include <string_view>
#include <expected>
namespace seele::meta {
template <std::integral T,
            std::integral U>
T safe_cast(U value) {
    if constexpr (sizeof(T) >= sizeof(U)) {
        return static_cast<T>(value);
    } else {
        if (value < std::numeric_limits<T>::min() || value > std::numeric_limits<T>::max()) {
            std::terminate(); // or throw an exception
        }
        return static_cast<T>(value);
    }
}



template <typename T, template <typename...> class Template>
struct is_specialization_of : std::false_type {};

template <template <typename...> class Template, typename... Args>
struct is_specialization_of<Template<Args...>, Template> : std::true_type {};

template <typename T, template <typename...> class Template>
constexpr bool is_specialization_of_v = is_specialization_of<T, Template>::value;


template <typename T>
struct __function_traits;

template<typename R, typename C, typename... A>
struct __function_traits<auto (C::*)(A...) -> R> {
    using class_t = C;
    using return_t = R;
    using args_t = std::tuple<A...>;
};

template<typename R, typename C, typename... A>
struct __function_traits<auto (C::*)(A...) const -> R> {
    using class_t = C;
    using return_t = R;
    using args_t = std::tuple<A...>;
};

template<typename R, typename C, typename... A>
struct __function_traits<auto (C::*)(A...) noexcept -> R> {
    using class_t = C;
    using return_t = R;
    using args_t = std::tuple<A...>;
};

template<typename R, typename C, typename... A>
struct __function_traits<auto (C::*)(A...) const noexcept -> R> {
    using class_t = C;
    using return_t = R;
    using args_t = std::tuple<A...>;
};

template<typename R, typename... A>
struct __function_traits<auto (*)(A...) -> R> {
    using class_t = void; 
    using return_t = R;
    using args_t = std::tuple<A...>;
};


template<typename R, typename... A>
struct __function_traits<auto (*)(A...) noexcept -> R> {
    using class_t = void; 
    using return_t = R;
    using args_t = std::tuple<A...>;
};
template<typename F>
struct __function_traits : __function_traits<decltype(&F::operator())> {};

template<typename F>
struct function_traits : __function_traits<std::decay_t<F>> {};





template <size_t I, template <typename...> typename tuple_t, typename... args_t>
auto tuple_erase(const tuple_t<args_t...>& t) {
    static_assert(I < sizeof...(args_t), "Index out of bounds for tuple_erase.");

    auto prefix = [&]<size_t... J>(std::index_sequence<J...>) {
        return std::forward_as_tuple(std::get<J>(t)...);
    }(std::make_index_sequence<I>());
    if constexpr (I == sizeof...(args_t) - 1) {
        return prefix;
    } else {
        auto suffix = [&]<size_t... J>(std::index_sequence<J...>) {
            return std::forward_as_tuple(std::get<J + I + 1>(t)...);
        }(std::make_index_sequence<sizeof...(args_t) - I - 1>());
        return std::tuple_cat(prefix, suffix);
    }

}





template <typename... args_t>
struct type_pack{};


template <size_t start, size_t end, typename... args_t>
    requires (start <= end && end <= sizeof...(args_t))
struct type_slice{
    template<size_t... I>
    static constexpr auto apply_impl(std::index_sequence<I...>) {
        return type_pack<std::tuple_element_t<start + I, std::tuple<args_t...>>...>{};
    }
    
    using types = decltype(apply_impl(std::make_index_sequence<end - start>{}));
};

template <size_t start, size_t end, typename... args_t>
using type_slice_t = typename type_slice<start, end, args_t...>::types;


template <auto T>
consteval std::string_view type_name() {
    std::string_view name = 
        #if defined(__clang__) || defined(__GNUC__)
            __PRETTY_FUNCTION__;  // Clang / GCC
        #else
            static_assert(false, "Unsupported compiler");
        #endif
    
#if defined(__clang__)
    constexpr std::string_view prefix = "std::string_view seele::meta::type_name() [auto T = ";
    constexpr std::string_view suffix = "]";
#elif defined(__GNUC__)
    constexpr std::string_view prefix = "consteval std::string_view seele::meta::type_name() [with auto T = ";
    constexpr std::string_view suffix = "; std::string_view = std::basic_string_view<char>]";
#endif
    name.remove_prefix(prefix.size());
    name.remove_suffix(suffix.size());
    return name;
}
template <typename T>
consteval std::string_view type_name() {
    std::string_view name = 
        #if defined(__clang__) || defined(__GNUC__)
            __PRETTY_FUNCTION__;  // Clang / GCC
        #else
            static_assert(false, "Unsupported compiler");
        #endif
    
#if defined(__clang__)
    constexpr std::string_view prefix = "std::string_view seele::meta::type_name() [T = ";
    constexpr std::string_view suffix = "]";
#elif defined(__GNUC__)
    constexpr std::string_view prefix = "consteval std::string_view seele::meta::type_name() [with T = ";
    constexpr std::string_view suffix = "; std::string_view = std::basic_string_view<char>]";
#endif
    name.remove_prefix(prefix.size());
    name.remove_suffix(suffix.size());
    return name;
}

template <typename T, size_t I = 0>
consteval auto count_enum_values() {
    if constexpr (type_name<static_cast<T>(I)>().find('(') != std::string_view::npos) {
        return I;
    } else {
        return count_enum_values<T, I + 1>();
    }
}

template <typename T>
consteval auto enum_name_table() {
    static_assert(std::is_enum_v<T>, "T must be an enum type");
    constexpr auto count = count_enum_values<T>();

    return [&]<size_t... Is>(std::index_sequence<Is...>) {
        auto extract_name = [](std::string_view name) {
            auto pos = name.find_last_of("::");
            if (pos != std::string_view::npos) {
                return name.substr(pos + 1);
            }
            return name;
        };

        return std::array{extract_name(type_name<static_cast<T>(Is)>())...};
    }(std::make_index_sequence<count>{});

}

template <typename T>
std::optional<T> enum_from_string(std::string_view str) {
    static_assert(std::is_enum_v<T>, "T must be an enum type");
    for (auto [index, name] : enum_name_table<T>() | std::views::enumerate) {
        if (name == str) {
            return static_cast<T>(index);
        }
    }
        
    return std::nullopt;
}
template <typename T>
std::string_view enum_to_string(T value) {
    static_assert(std::is_enum_v<T>, "T must be an enum type");
    constexpr auto names = enum_name_table<T>();
    return names.at(static_cast<size_t>(value));
}



constexpr size_t nops = -1;

template <typename T, typename lambda_t>
concept __visitable = 
    std::is_invocable_v<lambda_t, T> &&
    std::is_same_v<T, std::tuple_element_t<0, typename function_traits<lambda_t>::args_t>>;

template <typename T, typename... lambda_t>
concept __visitable_from = (__visitable<T, lambda_t> || ...);

template <typename T, typename lambda_t, typename... lambdas_t>
constexpr size_t __visitable_from_index(size_t count = 0) {
    if constexpr (__visitable_from<T, lambda_t>) {
        return count;
    } else if constexpr (sizeof...(lambdas_t) == 0) {
        return nops; // No valid lambda provided
    } else {
        return __visitable_from_index<T, lambdas_t...>(count + 1);
    }
}

template <typename T>
constexpr size_t __visitable_from_index() {
    return nops; // No valid lambda provided
}

template <typename variant_t, typename... lambdas_t>
constexpr bool __var_visitable_from = false;


template <typename... args_t, typename... lambdas_t>
constexpr bool __var_visitable_from<std::variant<args_t...>&, lambdas_t...> = (__visitable_from<args_t&, lambdas_t...> && ...);

template <typename... args_t, typename... lambdas_t>
constexpr bool __var_visitable_from<std::variant<args_t...>&&, lambdas_t...> = (__visitable_from<args_t&&, lambdas_t...> && ...);

template <typename... args_t, typename... lambdas_t>
constexpr bool __var_visitable_from<const std::variant<args_t...>&, lambdas_t...> = (__visitable_from<const args_t&, lambdas_t...> && ...);

template <typename variant_t, typename lambda_t>
struct __is_fallback_lambda: std::false_type{};

template <template <typename...> typename variant_t, typename lambda_t, typename... args_t>
    requires is_specialization_of_v<variant_t<args_t...>, std::variant>
struct __is_fallback_lambda<variant_t<args_t...>, lambda_t> 
    :std::bool_constant<
        (std::is_invocable_v<lambda_t, args_t> && ...)
    > {};

template <typename variant_t, typename lambda_t>
struct _is_fallback_lambda : __is_fallback_lambda<std::decay_t<variant_t>, std::decay_t<lambda_t>> {};

template <typename variant_t, typename lambda_t>
constexpr bool _is_fallback_lambda_v = _is_fallback_lambda<variant_t, lambda_t>::value;

template <typename variant_t, typename lambda_t, typename... lambdas_t>
constexpr size_t __fallback_lambda_index(size_t count = 0) {
    if constexpr (_is_fallback_lambda_v<variant_t, lambda_t>) {
        return count;
    } else if constexpr (sizeof...(lambdas_t) == 0) {
        return nops; // No valid lambda provided
    } else {
        return __fallback_lambda_index<variant_t, lambdas_t...>(count + 1);
    }
}




template <typename... lambdas_t>
struct hdlrs{
    std::tuple<lambdas_t...> lambdas;
    hdlrs(lambdas_t&&... ls) : lambdas(std::forward<lambdas_t>(ls)...) {}
};

template <typename T>
struct match{
    static_assert(false, "Unsupported type for match operation. ");
};

template <typename variant_t>
    requires is_specialization_of_v<std::decay_t<variant_t>, std::variant>
struct match<variant_t>{
    variant_t var;

    template<typename... lambdas_t>
    auto operator|(hdlrs<lambdas_t...>&& h){
        constexpr size_t fallback_index = __fallback_lambda_index<variant_t, lambdas_t...>();

        auto& visitors = h.lambdas;
        if constexpr (fallback_index == nops) {
            static_assert(
                __var_visitable_from<variant_t, lambdas_t...>,
                "Each variant alternative must be visitable by at least one of the provided lambdas. "
                "Check that lambda argument types are compatible with the value categories "
                "(e.g., T&, const T&, T&&) of the variant's alternatives."
            );        
            
            auto visitor = [&]<typename T>(T&& arg) {            
                return std::get<__visitable_from_index<T&&, lambdas_t...>()>(visitors)(std::forward<T>(arg));
            };

            if constexpr (std::is_lvalue_reference_v<variant_t>) {
                return std::visit(visitor, var);
            } else if constexpr (std::is_rvalue_reference_v<variant_t>) {
                return std::visit(visitor, std::move(var));
            } else {
                static_assert(false, "Unsupported variant type for match operation.");
            }
        } else {
            auto&& fallback_lambda = std::get<fallback_index>(visitors);

            auto visitor = [&]<typename T>(T&& arg) {

                constexpr size_t A_index = []<typename... A_args>(type_pack<A_args...>) {
                    return __visitable_from_index<T&&, A_args...>();
                }(type_slice_t<0, fallback_index, lambdas_t...>{});

                constexpr size_t B_index = []<typename... B_args>(type_pack<B_args...>) {
                    return __visitable_from_index<T&&, B_args...>();
                }(type_slice_t<fallback_index + 1, sizeof...(lambdas_t), lambdas_t...>{});

                if constexpr (A_index != nops) {
                    return std::get<A_index>(visitors)(std::forward<T>(arg));
                } else if constexpr (B_index != nops) {
                    return std::get<B_index + fallback_index + 1>(visitors)(std::forward<T>(arg));
                } else {
                    return fallback_lambda(std::forward<T>(arg));
                }
            };

            if constexpr (std::is_lvalue_reference_v<variant_t>) {
                return std::visit(visitor, var);
            } else if constexpr (std::is_rvalue_reference_v<variant_t>) {
                return std::visit(visitor, std::move(var));
            } else {
                static_assert(false, "Unsupported variant type for match operation.");
            }

        }
    }
    
    template<typename... lambdas_t>
    auto then(lambdas_t&&... h){
        return this->operator|(hdlrs(std::forward<lambdas_t>(h)...));
    }
};


template <typename T>
struct expected_traits {
    static_assert(false, "Unsupported type for expected traits. ");
};

template <typename T, typename E>
struct expected_traits<std::expected<T, E>&> {
    using Tp = T&;
    using Er = E&;
};

template <typename T, typename E>
struct expected_traits<std::expected<T, E>&&> {
    using Tp = T&&;
    using Er = E&&;
};

template <typename T, typename E>
struct expected_traits<const std::expected<T, E>&> {
    using Tp = const T&;
    using Er = const E&;
};


template <typename expected_t>
    requires is_specialization_of_v<std::decay_t<expected_t>, std::expected>
struct match<expected_t> {
    expected_t exp;
    template<typename... lambdas_t>
    auto operator|(hdlrs<lambdas_t...>&& h){
        using Tp = typename expected_traits<expected_t>::Tp;
        using Er = typename expected_traits<expected_t>::Er;

        constexpr size_t Tp_handle_index = __visitable_from_index<Tp, lambdas_t...>();
        constexpr size_t Er_handle_index = __visitable_from_index<Er, lambdas_t...>();

        if (exp.has_value()) {
            static_assert(Tp_handle_index != nops, 
                "No handler provided for the expected value type. "
                "Check that lambda argument types are compatible with the value category of the expected's value type.");
            auto& visitors = h.lambdas;
            auto visitor = [&]<typename T>(T&& arg) {
                return std::get<Tp_handle_index>(visitors)(std::forward<T>(arg));
            };
            if constexpr (std::is_lvalue_reference_v<expected_t>) {
                return visitor(exp.value());
            } else if constexpr (std::is_rvalue_reference_v<expected_t>) {
                return visitor(std::move(exp.value()));
            } else {
                static_assert(false, "Unsupported expected type for match operation.");
            }
        } else {
            static_assert(Er_handle_index != nops, 
                "No handler provided for the expected error type. "
                "Check that lambda argument types are compatible with the value category of the expected's error type.");
            auto& visitors = h.lambdas;
            auto visitor = [&]<typename T>(T&& arg) {
                return std::get<Er_handle_index>(visitors)(std::forward<T>(arg));
            };
            if constexpr (std::is_lvalue_reference_v<expected_t>) {
                return visitor(exp.error());
            } else if constexpr (std::is_rvalue_reference_v<expected_t>) {
                return visitor(std::move(exp.error()));
            } else {
                static_assert(false, "Unsupported expected type for match operation.");
            }
        }
    }

    template<typename... lambdas_t>
    auto then(lambdas_t&&... h){
        return this->operator|(hdlrs(std::forward<lambdas_t>(h)...));
    }
};


template <typename optional_t>
struct optional_traits {
    static_assert(false, "Unsupported type for expected traits. ");
};


template <typename optional_t>
struct optional_traits<std::optional<optional_t>&> {
    using Tp = optional_t&;
};

template <typename optional_t>
struct optional_traits<std::optional<optional_t>&&> {
    using Tp = optional_t&&;
};

template <typename optional_t>
struct optional_traits<const std::optional<optional_t>&> {
    using Tp = const optional_t&;
};

template <typename optional_t>
    requires is_specialization_of_v<std::decay_t<optional_t>, std::optional>
struct match<optional_t>{
    optional_t opt;
        template<typename... lambdas_t>
    auto operator|(hdlrs<lambdas_t...>&& h){
        using Tp = typename optional_traits<optional_t>::Tp;

        constexpr size_t Tp_handle_index = __visitable_from_index<Tp, lambdas_t...>();
        constexpr size_t Er_handle_index = __visitable_from_index<std::nullopt_t, lambdas_t...>();

        if (opt.has_value()) {
            static_assert(Tp_handle_index != nops, 
                "No handler provided for the expected value type. "
                "Check that lambda argument types are compatible with the value category of the expected's value type.");
            auto& visitors = h.lambdas;
            auto visitor = [&]<typename T>(T&& arg) {
                return std::get<Tp_handle_index>(visitors)(std::forward<T>(arg));
            };
            if constexpr (std::is_lvalue_reference_v<optional_t>) {
                return visitor(opt.value());
            } else if constexpr (std::is_rvalue_reference_v<optional_t>) {
                return visitor(std::move(opt.value()));
            } else {
                static_assert(false, "Unsupported expected type for match operation.");
            }
        } else {
            static_assert(Er_handle_index != nops, 
                "No handler provided for the expected error type. "
                "Check that lambda argument types are compatible with the value category of the expected's error type.");
            auto& visitors = h.lambdas;
            auto visitor = [&]<typename T>(T&& arg) {
                return std::get<Er_handle_index>(visitors)(std::forward<T>(arg));
            };
            if constexpr (std::is_lvalue_reference_v<optional_t>) {
                return visitor(std::nullopt);
            } else if constexpr (std::is_rvalue_reference_v<optional_t>) {
                return visitor(std::move(std::nullopt));
            } else {
                static_assert(false, "Unsupported expected type for match operation.");
            }
        }
    }
};


template <typename T>
match(T&) -> match<T&>;

template <typename T>
match(T&&) -> match<T&&>;

template <typename T>
match(const T&) -> match<T&>;
}
