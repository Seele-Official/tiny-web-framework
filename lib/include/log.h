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
#include "coro/async.h"
#include "meta.h"
namespace seele::log{
    using clock = std::chrono::system_clock;
    using soc_loc = std::source_location;
    constexpr size_t log_level = 0;

    enum class level {
        error,
        warn,
        info,
        debug,
        trace
    };

    class logger_impl {
    public:
        friend class async;
        friend class sync;
        logger_impl(const logger_impl&) = delete;        
        logger_impl(logger_impl&&) = delete;
        logger_impl& operator=(const logger_impl&) = delete;
        logger_impl& operator=(logger_impl&&) = delete;


        inline static logger_impl& get_instance() {
            static logger_impl inst;
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
    
        inline explicit logger_impl() : output{&std::cout} {}
        inline ~logger_impl() {
            if (output != &std::cout) {
                delete output;
            }
        }
  
        std::mutex mutex;    
        std::ostream* output;        
    };

    inline auto& logger() {
        return logger_impl::get_instance();
    }

    template<typename... args_t>
    void logger_impl::log(level lvl, soc_loc loc, clock::time_point time, std::format_string<args_t...> fmt, args_t&&... args){
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

    struct sync{
        soc_loc loc;
        clock::time_point now;
        sync(soc_loc loc = soc_loc::current(), clock::time_point now = clock::now()) : loc{loc}, now{now} {}
        template<typename... args_t>
        void log(level lvl, std::format_string<args_t...> fmt, args_t&&... args){
            if constexpr (log_level != 0){
                logger().log(
                    lvl, loc, now, fmt, std::forward<args_t>(args)...
                );
            }
        }

        template<typename... args_t>
        void error(std::format_string<args_t...> fmt, args_t&&... args){
            if constexpr (log_level != 0){
                this->log(
                    level::error, fmt, std::forward<args_t>(args)...
                );
            }
        }

        template<typename... args_t>
        void warn(std::format_string<args_t...> fmt, args_t&&... args){
            if constexpr (log_level != 0){
                this->log(
                    level::warn, fmt, std::forward<args_t>(args)...
                );
            }
        }

        template<typename... args_t>
        void info(std::format_string<args_t...> fmt, args_t&&... args){
            if constexpr (log_level != 0){
                this->log(
                    level::info, fmt, std::forward<args_t>(args)...
                );
            }
        }

        template<typename... args_t>
        void debug(std::format_string<args_t...> fmt, args_t&&... args){
            if constexpr (log_level > 1){
                this->log(
                    level::debug, fmt, std::forward<args_t>(args)...
                );
            }
        }

        template<typename... args_t>
        void trace(std::format_string<args_t...> fmt, args_t&&... args){
            if constexpr (log_level > 1){
                this->log(
                    level::trace, fmt, std::forward<args_t>(args)...
                );
            }
        }
    };

    struct async : sync{
        async(soc_loc loc = soc_loc::current(), clock::time_point now = clock::now()) : sync{loc, now} {}

        template<typename... args_t>
        void log(level lvl, std::format_string<std::decay_t<args_t>&...> fmt, args_t&&... args){
            if constexpr (log_level != 0){
                seele::coro::async(
                    &logger_impl::log<std::decay_t<args_t>&...>,
                    std::ref(logger()), lvl, loc, now, fmt, std::forward<args_t>(args)...
                );
            }

        }
        template<typename... args_t>
        void error(std::format_string<std::decay_t<args_t>&...> fmt, args_t&&... args){
            if constexpr (log_level != 0){
                this->log(
                    level::error, fmt, std::forward<args_t>(args)...
                );
            }
        }

        template<typename... args_t>
        void warn(std::format_string<std::decay_t<args_t>&...> fmt, args_t&&... args){
            if constexpr (log_level != 0){
                this->log(
                    level::warn, fmt, std::forward<args_t>(args)...
                );
            }
        }

        template<typename... args_t>
        void info(std::format_string<std::decay_t<args_t>&...> fmt, args_t&&... args){
            if constexpr (log_level != 0){
                this->log(
                    level::info, fmt, std::forward<args_t>(args)...
                );
            }
        }

        template<typename... args_t>
        void debug(std::format_string<std::decay_t<args_t>&...> fmt, args_t&&... args){
            if constexpr (log_level > 1){
                this->log(
                    level::debug, fmt, std::forward<args_t>(args)...
                );
            }
        }

        template<typename... args_t>
        void trace(std::format_string<std::decay_t<args_t>&...> fmt, args_t&&... args){
            if constexpr (log_level > 1){
                this->log(
                    level::trace, fmt, std::forward<args_t>(args)...
                );
            }
        }

    };
    

}

