#pragma once
#include <chrono>
#include <format>
#include <source_location>
#include <concepts>
namespace seele::log{

enum class level{
    error,
    warn,
    info,
    debug,
    trace,
};

constexpr level  max_level = level::info;
constexpr size_t packet_batch_size = 256;

template <typename... args_t>
struct basic_format_string_wrapper {
    template<typename T>
	    requires std::convertible_to<const T&, std::string_view>
	consteval basic_format_string_wrapper(const T& s, 
                    std::source_location loc = std::source_location::current()) : loc{loc}, fmt{s} {}
    std::source_location          loc;
    std::format_string<args_t...> fmt;
};
template <typename... args_t>
using format_string_wrapper = basic_format_string_wrapper<std::unwrap_ref_decay_t<std::type_identity_t<args_t>>&...>;

using system_clock = std::chrono::system_clock;
}