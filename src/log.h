#pragma once
#include <cstddef>
#include <iostream>
#include <fstream>
#include <memory>
#include <mutex>
#include <format>
#include <ostream>
#include <source_location>
#include <functional>
#include <string_view>
#include <print>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <chrono>
#include <format>
#include <vector>
#include "meta.h"
#include "concurrent/mpsc_ringbuffer.h"
namespace seele::log{
using namespace std::literals;
enum class level{
    error,
    warn,
    info,
    debug,
    trace,
};

constexpr level log_level = level::info;
constexpr size_t packet_batch_size = 256;

namespace detail {

class packet{
public:
    using deletor_signature = void(void*);
    using logfunc_signature = void(void*);
    packet() = default;
    packet(void* ctx, logfunc_signature* logfunc, deletor_signature* deletor)
        : ctx{ctx}, logfunc{logfunc}, deletor{deletor} {}
    packet(const packet&) = delete;
    packet(packet&& o) 
        : ctx{std::exchange(o.ctx, nullptr)}, logfunc{std::exchange(o.logfunc, nullptr)}, deletor{std::exchange(o.deletor, nullptr)} {}
    packet& operator=(const packet&) = delete;
    packet& operator=(packet&& o) {
        if (this != &o) {
            if (deletor && ctx) {
                deletor(ctx);
            }
            ctx = std::exchange(o.ctx, nullptr);
            logfunc = std::exchange(o.logfunc, nullptr);
            deletor = std::exchange(o.deletor, nullptr);
        }
        return *this;
    }

    ~packet(){
        if (deletor && ctx) {
            deletor(ctx);
        }
    }

    template<level lvl, typename... args_t>
    static packet make(args_t&&... args);

    void log() const {
        if (logfunc && ctx) {
            logfunc(ctx);
        }
    }

private:    
    void*              ctx{nullptr};
    logfunc_signature* logfunc{nullptr};
    deletor_signature* deletor{nullptr};
};

class logger{
public:
    logger() = default;
    logger(const logger&) = delete;        
    logger(logger&&) = delete;
    logger& operator=(const logger&) = delete;
    logger& operator=(logger&&) = delete;

    ~logger() {
        while (this->ringbuffer.size() > 0) {
            std::this_thread::sleep_for(10ms);
        }
        this->worker_thread.request_stop();
        this->sem.release();        
        
        if (output != &std::cout) {
            delete output;
        }
    }

    static auto& get_instance() {
        static auto inst = std::make_shared<logger>();
        return inst;
    }

    void set_output(std::ostream* os) {
        std::lock_guard lock(this->mutex);
        this->output = os;
    }

    bool submit_packets(std::vector<packet>&& pkts) {
        if (ringbuffer.emplace_back(std::move(pkts))) {
            sem.release();
            return true;
        }
        return false;
    }
private:
    void loop(std::stop_token st){
        while(sem.acquire(), !st.stop_requested()){
            auto pkts = ringbuffer.unsafe_pop_front();
            for (auto& p : pkts) {
                p.log();
            }
        }
    }

    alignas(64) std::counting_semaphore<> sem{0};
    concurrent::mpsc_ringbuffer<std::vector<packet>, 1024> ringbuffer{};
    std::jthread worker_thread{
        [this](std::stop_token st){
            this->loop(st);
        }
    };
public:
    std::mutex    mutex{};    
    std::ostream* output{&std::cout};
};

template<level lvl, typename... args_t>
void log(
    logger&                               logger,
    std::source_location                  loc, 
    std::chrono::system_clock::time_point time, 
    std::format_string<args_t...>         fmt, 
    args_t&&...                           args
){
    if constexpr (lvl <= log_level) {
        std::lock_guard lock(logger.mutex);
        std::println(
            *logger.output,
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

class tls_t{
public:
    tls_t(){
        this->packets.reserve(packet_batch_size);
    }
    ~tls_t(){
        if (!this->packets.empty()) {
            this->submit();
        }
    }
    void push(packet&& p){
        this->packets.push_back(std::move(p));
        if (this->packets.size() >= packet_batch_size) {
            this->submit();
        }
    }
private:
    void submit(){
        if (logger->submit_packets(std::move(packets))) {
            packets.reserve(packet_batch_size);
        } else {
            std::println("log: packet drop");
        }
    }
public:
    std::shared_ptr<detail::logger> logger{detail::logger::get_instance()};
private:    
    std::vector<packet> packets{};
};

inline auto& tls(){
    thread_local static tls_t e{};
    return e;
}

template<level lvl, typename... args_t>
packet packet::make(args_t&&... args){
    using ctx_t = decltype(std::tuple(std::forward<args_t>(args)...));
    auto* ctx = new ctx_t(std::forward<args_t>(args)...);
    return packet{
        ctx,
        [](void* ctx){
            auto* d = static_cast<ctx_t*>(ctx);
            [&]<size_t... I>(std::index_sequence<I...>){
                detail::log<lvl>(
                    *tls().logger,
                    std::get<I>(*d)...
                );
            }(std::make_index_sequence<sizeof...(args_t)>{});
        },
        [](void* ctx){
            delete static_cast<ctx_t*>(ctx);
        }
    };
}
} //namespace detail

inline void set_output_file(std::string_view filename) {
    detail::logger::get_instance()->set_output(new std::ofstream{filename.data(), std::ios::app});
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


namespace sync {
template <typename... args_t>
using format_string_wrapper = basic_format_string_wrapper<std::type_identity_t<args_t>...>;

template<level lvl, typename... args_t>
void log(format_string_wrapper<args_t...> fmt_w, args_t&&... args){
    if constexpr (lvl <= log_level) {
        auto [loc, fmt] = fmt_w;
        detail::log<lvl>(
            *detail::logger::get_instance(),
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
}//namespace sync


namespace async{
template <typename... args_t>
using format_string_wrapper = basic_format_string_wrapper<std::unwrap_ref_decay_t<std::type_identity_t<args_t>>&...>;


template<level lvl, typename... args_t>
void log(format_string_wrapper<args_t...> fmt_w, args_t&&... args){
    if constexpr (lvl <= log_level) {
        auto [loc, fmt] = fmt_w;
        detail::tls().push(
            detail::packet::make<lvl>(loc, std::chrono::system_clock::now(), fmt, std::forward<args_t>(args)...)
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

}//namespace async

}

