#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
namespace seele::concurrent {
    
namespace hp {
    constexpr size_t max_hazard_count = 3;
    constexpr size_t max_thread_count = 64;
    constexpr size_t max_retired_count = 16;    
}


namespace detail{
class hazard_recorder{
public:
    hazard_recorder() = default;
    ~hazard_recorder();

    struct alignas(64) record_t {
        std::array<std::atomic<void*>, hp::max_hazard_count> hps;
        std::atomic<bool> active;
    };

    struct retired_ptr_t {
        void* ptr;
        auto (*deleter)(void*) -> void;
    };
    record_t* allocate_record();
    void deallocate_record(record_t* record);

    void scan_retired(std::vector<retired_ptr_t>& retired_list);

    void collect_unretired(std::vector<retired_ptr_t>& retireds); 
private:

    std::array<record_t, hp::max_thread_count> records;
    std::vector<retired_ptr_t> g_retired;
    std::mutex g_retired_mutex;
};
};


class hazard_manager {
public:        
    hazard_manager() : recorder(std::make_shared<detail::hazard_recorder>()) {}

    hazard_manager(const hazard_manager&) = delete;
    hazard_manager& operator=(const hazard_manager&) = delete;
    hazard_manager(hazard_manager&&) = delete;
    hazard_manager& operator=(hazard_manager&&) = delete;

    ~hazard_manager() = default;

    template<size_t index>
        requires (index < hp::max_hazard_count)
    void protect(void* ptr){
        this->local_tls().record
            ->hps[index].store(ptr, std::memory_order_release);
    }


    template<size_t index>
        requires (index < hp::max_hazard_count)
    void clear(){
        this->local_tls().record
            ->hps[index].store(nullptr, std::memory_order_release);
    }

    void clear_all();

    template<typename T>
    void retire(T* ptr){
        this->local_tls().retired_list.emplace_back(ptr, [](void* p){ delete static_cast<T*>(p); });
        this->scan_tls_retired();
    }

    template<typename T>
    void retire(T* ptr, auto (*deleter)(void*) -> void){
        this->local_tls().retired_list.emplace_back(ptr, deleter);
        this->scan_tls_retired();
    }
private:
    using record_t = detail::hazard_recorder::record_t;
    using retired_ptr_t = detail::hazard_recorder::retired_ptr_t;

    struct tls_data_t {
        record_t* record;
        std::vector<retired_ptr_t> retired_list;
    };

    struct tls_t {
        tls_t() = default;
        ~tls_t();        
        std::unordered_map<
            std::shared_ptr<detail::hazard_recorder>,
            tls_data_t
        > map{};
    };

    tls_data_t& local_tls();

    void scan_tls_retired();


    std::shared_ptr<detail::hazard_recorder> recorder;
};    



} // namespace seele::concurrent
