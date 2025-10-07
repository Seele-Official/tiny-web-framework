#pragma once
#include <cstddef>
#include <iostream>
#include <fstream>
#include <mutex>
#include <format>
#include <ostream>
#include <source_location>
#include <string_view>
#include <print>
#include <utility>
#include <chrono>
#include <format>
#include "meta.h"
namespace seele::log{

enum class level{
    error,
    warn,
    info,
    debug,
    trace,
};

constexpr level log_level = level::info;

namespace detail {

class logger{
public:
    logger(const logger&) = delete;        
    logger(logger&&) = delete;
    logger& operator=(const logger&) = delete;
    logger& operator=(logger&&) = delete;


    inline static logger& get_instance() {
        static logger inst;
        return inst;
    }

    inline void set_output(std::ostream& os) {
        this->output = &os;
    }

    inline void set_output_file(std::string_view filename) {
        this->output = new std::ofstream{filename.data(), std::ios::app};
    }

    template<level lvl, typename... args_t>
    void log(
        std::source_location loc,
        std::chrono::system_clock::time_point time,
        std::format_string<args_t...> fmt,
        args_t&&... args
    ); 

private:    
    inline explicit logger() : output{&std::cout} {}
    inline ~logger() {
        if (output != &std::cout) {
            delete output;
        }
    }

    std::mutex    mutex;    
    std::ostream* output;        
};

template<level lvl, typename... args_t>
void logger::log(
    std::source_location                  loc, 
    std::chrono::system_clock::time_point time, 
    std::format_string<args_t...>         fmt, 
    args_t&&...                           args
){
    if constexpr (lvl <= log_level) {
        std::lock_guard lock(this->mutex);

        std::println(
            *this->output,
            "[{}] [{:%Y-%m-%d %H:%M:%S}]: `{}` at {}:{}:{}",
            meta::enum_name<lvl>(), 
            time,
            std::format(
                fmt, std::forward<args_t>(args)...
            ),
            loc.file_name(), loc.line(), loc.column()
        );

        
    }
}
};

inline void set_output_file(std::string_view filename) {
    detail::logger::get_instance().set_output_file(filename);
}


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
using format_string_wrapper = basic_format_string_wrapper<std::type_identity_t<args_t>...>;



namespace sync {
    template<level lvl, typename... args_t>
    void log(format_string_wrapper<args_t...> fmt_w, args_t&&... args){
        auto [loc, fmt] = fmt_w;
        if constexpr (lvl <= log_level) {
            detail::logger::get_instance().log<lvl>(
                loc, std::chrono::system_clock::now(), fmt, std::forward<args_t>(args)...
            );
        }
    }

    template<typename... args_t>
    void error(format_string_wrapper<args_t...> fmt_w, args_t&&... args){
        log<level::error>(fmt_w, std::forward<args_t>(args)...);
    }

    template<typename... args_t>
    void warn(format_string_wrapper<args_t...> fmt_w, args_t&&... args){
        log<level::warn>(fmt_w, std::forward<args_t>(args)...);
    }

    template<typename... args_t>
    void info(format_string_wrapper<args_t...> fmt_w, args_t&&... args){
        log<level::info>(fmt_w, std::forward<args_t>(args)...);
    }

    template<typename... args_t>
    void debug(format_string_wrapper<args_t...> fmt_w, args_t&&... args){
        log<level::debug>(fmt_w, std::forward<args_t>(args)...);
    }

    template<typename... args_t>
    void trace(format_string_wrapper<args_t...> fmt_w, args_t&&... args){
        log<level::trace>(fmt_w, std::forward<args_t>(args)...);
    }
};

// TODO: async logging
namespace async = sync;

}

