#pragma once
#include <optional>
#include <type_traits>
#include <utility>
#include <cstddef>
#include <ranges>
#include <string_view>
namespace meta {
template <auto T>
consteval std::string_view type_name() {
    std::string_view name = 
        #if defined(__clang__) || defined(__GNUC__)
            __PRETTY_FUNCTION__;  // Clang / GCC
        #else
            static_assert(false, "Unsupported compiler");
        #endif
    
#if defined(__clang__)
    constexpr std::string_view prefix = "std::string_view meta::type_name() [T = ";
    constexpr std::string_view suffix = "]";
#elif defined(__GNUC__)
    constexpr std::string_view prefix = "consteval std::string_view meta::type_name() [with auto T = ";
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
    constexpr std::string_view prefix = "std::string_view meta::type_name() [T = ";
    constexpr std::string_view suffix = "]";
#elif defined(__GNUC__)
    constexpr std::string_view prefix = "consteval std::string_view meta::type_name() [with T = ";
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

template<auto enum_value>
    requires std::is_enum_v<decltype(enum_value)>
consteval auto enum_name() {
    std::string_view name = type_name<enum_value>();
    auto pos = name.find_last_of("::");
    if (pos != std::string_view::npos) {
        return name.substr(pos + 1);
    }
    return name;
}

template <typename T>
consteval auto enum_name_table() {
    static_assert(std::is_enum_v<T>, "T must be an enum type");
    constexpr auto count = count_enum_values<T>();

    return [&]<size_t... Is>(std::index_sequence<Is...>) {
        return std::array{
            enum_name<static_cast<T>(Is)>()...
        };
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

}
