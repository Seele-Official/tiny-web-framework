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

using clock = std::chrono::system_clock;
using soc_loc = std::source_location;

enum class level {
    error,
    warn,
    info,
    debug,
    trace
};
constexpr size_t log_level = 1;

template <typename... args_t>
struct basic_format_string_wrapper {
    template<typename T>
	    requires std::convertible_to<const T&, std::string_view>
	consteval basic_format_string_wrapper(const T& s, 
                         soc_loc loc = soc_loc::current()) : fmt(s), loc(loc) {}
    std::format_string<args_t...> fmt;
    soc_loc loc;
};

template <typename... args_t>
using format_string_wrapper = basic_format_string_wrapper<std::type_identity_t<args_t>...>;

namespace sync {
    template<level lvl, typename... args_t>
    void log(format_string_wrapper<args_t...>, args_t&&...);
}

namespace detail{

class logger {
public:
    template<level lvl, typename... args_t>
    friend void sync::log(format_string_wrapper<args_t...> fmt_w, args_t&&... args);

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
    


private:    
    template<typename... args_t>
    void log(
        level level,
        std::source_location loc,
        std::chrono::system_clock::time_point time,
        std::format_string<args_t...> fmt,
        args_t&&... args
    );

    inline explicit logger() : output{&std::cout} {}
    inline ~logger() {
        if (output != &std::cout) {
            delete output;
        }
    }

    std::mutex mutex;    
    std::ostream* output;        
};

template<typename... args_t>
void logger::log(level lvl, soc_loc loc, clock::time_point time, std::format_string<args_t...> fmt, args_t&&... args){
    static auto lvl_map = seele::meta::enum_name_table<level>();
    if constexpr (log_level == 0){
        return; // No logging
    } else if constexpr (log_level == 1){
        if (lvl == level::trace || lvl == level::debug) {
            return; // No trace or debug logging
        }

        std::lock_guard lock(mutex);
        std::println(
            *this->output,
            "[{}] [{:%Y-%m-%d %H:%M:%S}] :{}",
            lvl_map[static_cast<int>(lvl)], time,
            std::format(
                fmt, std::forward<args_t>(args)...
            )
        );

    } else {
        if (lvl == level::trace || lvl == level::debug) {
            std::lock_guard lock(mutex);
            std::println(
                *this->output,
                "[{}] [{:%Y-%m-%d %H:%M:%S}] {}:{}:{} :{}",
                lvl_map[static_cast<int>(lvl)], time, loc.file_name(), loc.line(), loc.column(),
                std::format(
                    fmt, std::forward<args_t>(args)...
                )
            );
        } else {
            std::lock_guard lock(mutex);
            std::println(
                *this->output,
                "[{}] [{:%Y-%m-%d %H:%M:%S}] :{}",
                lvl_map[static_cast<int>(lvl)], time,
                std::format(
                    fmt, std::forward<args_t>(args)...
                )
            );                
        }
    }

}
};


inline auto& logger() {
    return detail::logger::get_instance();
}

namespace sync{
    template<level lvl, typename... args_t>
    void log(format_string_wrapper<args_t...> fmt_w, args_t&&... args){
        auto [fmt, loc] = fmt_w;
        if constexpr (log_level != 0){
            logger().log(
                lvl, loc, clock::now(), fmt, std::forward<args_t>(args)...
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

namespace async = sync;

}

