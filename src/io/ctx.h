#pragma once

#include <algorithm>
#include <atomic>
#include <coroutine>
#include <cstddef>
#include <liburing.h>
#include <cstdint>
#include <print>
#include <semaphore>
#include <thread>
#include <stop_token>
#include <cstring>
#include <utility>
#include "concurrent/mpsc_queue.h"
#include "concurrent/spsc_object_pool.h"


namespace io::detail {

constexpr size_t submit_threshold = 64;

// Why re-implement variant?
// Because std::variant uses non-atomic index, which may cause
// data-race in multi-threaded scenarios.
// Our variant implementation uses atomic index to avoid data-race.
template<typename... Ts>
    requires (std::is_same_v<Ts, std::decay_t<Ts>> && ...)
class variant{
public:
    using index_t = uint8_t;

    template<index_t N>
    struct type_at {
        using type = typename std::tuple_element<N, std::tuple<Ts...>>::type;
    };
    template<index_t N>
    using type_at_t = typename type_at<N>::type;

    template<typename T, typename... args_t>
    variant(std::in_place_type_t<T>, args_t&&... args) {
        static_assert((std::is_same_v<T, Ts> || ...), "Type T must be one of Ts...");
        new (storage) T(std::forward<args_t>(args)...);
        index.store(index_of<T>(), std::memory_order_release);
    }

    ~variant() {
        this->visit([]<typename T>(T& val){
            val.~T();
        });
    }

    template<typename T>
    static consteval index_t index_of() {
        index_t index = 0;
        auto _ = ((std::is_same_v<T, Ts> ? true : (index++, false)) || ...);
        return index;
    }


    static consteval size_t storage_size_impl() {
        std::array sizes = {sizeof(Ts)...};
        return *std::ranges::max_element(sizes);
    }
    index_t get_index() const {
        return index.load(std::memory_order_acquire);
    }

    template<typename T>
    T* get_if() {
        if (this->get_index() == index_of<T>()) {
            return std::launder(reinterpret_cast<T*>(storage));
        }
        return nullptr;
    }

    template<typename invocable_t>
    void visit(invocable_t&& invocable) {
        index_t idx = this->get_index();
        auto _ = ((
            idx == index_of<Ts>() ? 
            (
                // [&]<typename T>(T* ptr) {
                //     if constexpr (std::is_invocable_v<invocable_t, T&>) {
                //         invocable(*ptr);
                //     } else if constexpr (std::is_invocable_v<invocable_t, T>) {
                //         invocable(std::move(*ptr));
                //     } else {
                //         static_assert(sizeof(T) == 0, "Visitor is not invocable with the variant alternative type");
                //     }
                // }(std::launder(reinterpret_cast<Ts*>(storage)))
                invocable(*std::launder(reinterpret_cast<Ts*>(storage)))
                , true
            ) : false
        ) || ...);
    }

private:
    alignas(Ts...) std::byte storage[storage_size_impl()];
    std::atomic<index_t> index;
};

class ctx{
public:      
    struct request{        
        void* helper_ptr;
        auto (*ring_handle)(void*, io_uring*) -> int;
    };
    struct io_usr_data;
    struct timeout_usr_data;
    struct multishot_usr_data;
    using usr_data = variant<io_usr_data, timeout_usr_data>;

    struct io_usr_data{
        std::coroutine_handle<> handle;
        std::atomic<int32_t>* io_ret;
    };

    struct timeout_usr_data{
        usr_data* io_data;
    };

    struct multishot_usr_data{
        void (*callback)(int32_t);
    };


    ctx(const ctx&) = delete;
    ctx(ctx&&) = delete;
    ctx& operator=(const ctx&) = delete;
    ctx& operator=(ctx&&) = delete;

    template<typename... args_t>
    usr_data* new_usr_data(args_t&& ...args){
        usr_data* data_ptr;
        do {
            data_ptr = this->usr_data_pool.allocate(
                std::forward<args_t>(args)...
            );
        } while (data_ptr == nullptr);

        return data_ptr;
    }

    inline int32_t register_file_alloc_range(uint32_t off, uint32_t len) {
        return io_uring_register_file_alloc_range(&ring, off, len);
    }

    inline int32_t register_files_sparse(uint32_t count) {
        return io_uring_register_files_sparse(&ring, count);
    }

    inline int32_t register_files(const int32_t* fds, uint32_t count) {
        return io_uring_register_files(&ring, fds, count);
    }
    inline int32_t unregister_files() {
        return io_uring_unregister_files(&ring);
    }

    inline bool submit(void* helper_ptr, auto (*ring_handle)(void*, io_uring*) -> int) {
        if (this->is_worker_running.load(std::memory_order_acquire)){
            this->unprocessed_requests.emplace_back(helper_ptr, ring_handle);
            this->unp_sem.release();            
            return true;
        }
        return false;
    }


    inline void request_stop() { stop_src.request_stop(); }
    
    inline void run(){ 
        this->start_listen(stop_src.get_token());
        this->clean_up(); 
    }

    void clean_up();

    inline static ctx& get_instance() {
        static ctx instance;
        return instance;
    }
private: 

    void worker(std::stop_token st);

    void start_listen(std::stop_token st);

    void handle_cqes(io_uring_cqe* cqe);

    ctx(uint32_t entries = 128, uint32_t flags = 0) : 
        pending_req_count{0}, 
        unp_sem{0}, 
        usr_data_pool{1024*128} 
    {
        if(io_uring_queue_init(entries, &ring, flags) < 0) {
            std::println("Failed to initialize io_uring");
            std::terminate();
        }
        this->worker_thread = std::jthread([&] (std::stop_token st) { worker(st); }, stop_src.get_token());
        this->is_worker_running.store(true, std::memory_order_release);
    }
    ~ctx();  



    io_uring ring;
    std::stop_source stop_src;
    std::jthread worker_thread; 
    std::atomic<bool> is_worker_running;
    alignas(64) std::atomic<size_t> pending_req_count;

    alignas(64) std::counting_semaphore<> unp_sem;
    concurrent::mpsc_queue<request> unprocessed_requests;

    concurrent::spsc_object_pool<usr_data> usr_data_pool;
};

}
