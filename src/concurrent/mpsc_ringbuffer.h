#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>

namespace seele::concurrent {

template <typename T, size_t N = 64>
class mpsc_ringbuffer{
public:
    static_assert((N & (N - 1)) == 0, "N must be power of 2");
    mpsc_ringbuffer() = default;

    ~mpsc_ringbuffer();

    enum status_t {
        EMPTY,
        READY
    };
    struct node_t{
        alignas(T) std::byte storage[sizeof(T)];
        std::atomic<status_t> status;
        T& get() {
            return *std::launder(reinterpret_cast<T*>(&storage));
        }
        node_t() : storage{}, status(EMPTY) {}
    };

    size_t size() const {
        return write_index.load(std::memory_order_acquire) - read_index.load(std::memory_order_acquire);
    }

    T unsafe_pop_front();

    std::optional<T> pop_front();

    template<typename... args_t>
    bool emplace_back(args_t&&... args);


private:
    node_t& get_node(size_t index) {
        return buffer[index % N];
    }
    std::array<node_t, N> buffer{};
    alignas(64) std::atomic<size_t> read_index{0};
    alignas(64) std::atomic<size_t> write_index{0};
};

template<typename T, size_t N>
template<typename... args_t>
bool mpsc_ringbuffer<T, N>::emplace_back(args_t&&... args) {
    while (true) {
        size_t write_idx = write_index.load(std::memory_order_acquire);
        size_t read_idx = read_index.load(std::memory_order_acquire);
        if (write_idx - read_idx >= N) {
            return false; // buffer is full
        }
        if (this->write_index.compare_exchange_strong(write_idx, write_idx + 1, std::memory_order_release, std::memory_order_relaxed)){
            // construct new value in place
            new (&this->get_node(write_idx).storage) T(std::forward<args_t>(args)...);
            this->get_node(write_idx).status.store(READY, std::memory_order_release);
            return true; // successfully added
        }
    }
}


template<typename T, size_t N>
T mpsc_ringbuffer<T, N>::unsafe_pop_front(){
    size_t read_idx = read_index.load(std::memory_order_acquire);
    
    while (this->get_node(read_idx).status.load(std::memory_order_acquire) != READY) {
        // wait until the data is ready
    }

    T result = std::move(this->get_node(read_idx).get());
    this->get_node(read_idx).get().~T();
    this->get_node(read_idx).status.store(EMPTY, std::memory_order_release);
    this->read_index.fetch_add(1, std::memory_order_release);
    return result;
}


template<typename T, size_t N>
std::optional<T> mpsc_ringbuffer<T, N>::pop_front(){
    size_t write_idx = write_index.load(std::memory_order_acquire);
    size_t read_idx = read_index.load(std::memory_order_acquire);
    if (read_idx >= write_idx) {
        return std::nullopt; // no elements to pop
    }

    while (this->get_node(read_idx).status.load(std::memory_order_acquire) != READY) {
        // wait until the data is ready
    }

    T result = std::move(this->get_node(read_idx).get());
    this->get_node(read_idx).get().~T();
    this->get_node(read_idx).status.store(EMPTY, std::memory_order_release);
    this->read_index.fetch_add(1, std::memory_order_release);
    return result;
}

template<typename T, size_t N>
mpsc_ringbuffer<T, N>::~mpsc_ringbuffer(){
    size_t read_idx = read_index.load(std::memory_order_acquire);
    size_t write_idx_local = write_index.load(std::memory_order_acquire);
    for (size_t i = read_idx; i < write_idx_local; ++i) {
        auto& node = this->get_node(i);
        if (node.status.load(std::memory_order_acquire) == READY) {
            node.get().~T();
            node.status.store(EMPTY, std::memory_order_relaxed);
        }
    }
}

}