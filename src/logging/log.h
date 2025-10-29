#pragma once
#include <cstddef>
#include <memory>
#include <mutex>
#include <format>
#include <print>
#include <thread>
#include <tuple>
#include <utility>
#include <format>
#include <vector>

#include "logging/sink.h"
#include "logging/common.h"

#include "concurrent/mpsc_ringbuffer.h"

namespace logging {

namespace detail {

class packet{
public:
    using deletor_signature = void(void*);
    using fmtfunc_signature = void(void*, sink::basic&);
    packet() = default;
    packet(void* ctx, fmtfunc_signature* fmtfunc, deletor_signature* deletor)
        : ctx{ctx}, fmtfunc{fmtfunc}, deletor{deletor} {}
    packet(const packet&) = delete;
    packet(packet&& o) 
        : ctx{std::exchange(o.ctx, nullptr)}, fmtfunc{std::exchange(o.fmtfunc, nullptr)}, deletor{std::exchange(o.deletor, nullptr)} {}
    packet& operator=(const packet&) = delete;
    packet& operator=(packet&& o) {
        if (this != &o) {
            if (deletor && ctx) {
                deletor(ctx);
            }
            ctx = std::exchange(o.ctx, nullptr);
            fmtfunc = std::exchange(o.fmtfunc, nullptr);
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
    static packet make(system_clock::time_point time, args_t&&... args){
        return make_helper<lvl>(time, std::forward<args_t>(args)...);
    }

    void to(sink::basic& out) const {
        if (fmtfunc && ctx) {
            fmtfunc(ctx, out);
        }
    }
private:    
    template<level lvl, typename... args_t>
    static packet make_helper(args_t&&... args);

    void*              ctx{nullptr};
    fmtfunc_signature* fmtfunc{nullptr};
    deletor_signature* deletor{nullptr};
};


template<level lvl, typename... args_t>
packet packet::make_helper(args_t&&... args){
    using ctx_t = decltype(std::tuple(std::forward<args_t>(args)...));
    auto* ctx = new ctx_t(std::forward<args_t>(args)...);
    return packet{
        ctx,
        [](void* ctx, sink::basic& out){
            auto* d = static_cast<ctx_t*>(ctx);
            [&]<size_t... I>(std::index_sequence<I...>){
                out.log<lvl>(std::get<I>(*d)...);
            }(std::make_index_sequence<sizeof...(args_t)>{});
        },
        [](void* ctx){
            delete static_cast<ctx_t*>(ctx);
        }
    };
}


class logger{
public:
    logger() = default;
    logger(const logger&) = delete;        
    logger(logger&&) = delete;
    logger& operator=(const logger&) = delete;
    logger& operator=(logger&&) = delete;

    ~logger() {
        using namespace std::chrono_literals;
        while (this->ringbuffer.size() > 0) {
            std::this_thread::sleep_for(10ms);
        }
        this->worker_thread.request_stop();
        this->sem.release(); 
    }

    static auto& get_instance() {
        static auto inst = std::make_shared<logger>();
        return inst;
    }

    void add_sink(std::unique_ptr<sink::basic> s) {
        std::lock_guard lock{mutex};
        sinks.push_back(std::move(s));
    }

    void add_sinks(std::vector<std::unique_ptr<sink::basic>> ss) {
        std::lock_guard lock{mutex};
        for (auto& s : ss) {
            sinks.push_back(std::move(s));
        }
    }

    void log(const packet& p) {
        std::lock_guard lock{mutex};
        for (auto& sink : sinks) {
            p.to(*sink);
        }
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
                for (auto& sink : sinks) {
                    p.to(*sink);
                }
            }
        }
    }
    std::mutex    mutex{};    
    std::vector<std::unique_ptr<sink::basic>> sinks{};

    alignas(64) std::counting_semaphore<> sem{0};
    concurrent::mpsc_ringbuffer<std::vector<packet>, 1024> ringbuffer{};
    std::jthread worker_thread{
        [this](std::stop_token st){
            this->loop(st);
        }
    };
};


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


} //namespace detail

inline void add_sink(std::unique_ptr<sink::basic> s) {
    detail::logger::get_instance()->add_sink(std::move(s));
}


namespace sync {
template<level lvl, typename... args_t>
void log(format_string_wrapper<args_t...> fmt_w, args_t&&... args){
    if constexpr (lvl <= logging::max_level) {
        detail::logger::get_instance()->log(
            detail::packet::make<lvl>(system_clock::now(), fmt_w, std::forward<args_t>(args)...)
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

template<level lvl, typename... args_t>
void log(format_string_wrapper<args_t...> fmt_w, args_t&&... args){
    if constexpr (lvl <= logging::max_level) {
        detail::tls().push(
            detail::packet::make<lvl>(system_clock::now(), fmt_w, std::forward<args_t>(args)...)
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

