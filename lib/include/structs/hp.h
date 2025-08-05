#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <list>
#include <mutex>
#include <unordered_map>
namespace seele::structs {
    
namespace hp {
    constexpr size_t max_hazard_count = 3;
    constexpr size_t max_thread_count = 64;
    constexpr size_t max_retired_count = 16;    
}

class hazard_manager {
public:        
    hazard_manager() = default;

    hazard_manager(const hazard_manager&) = delete;
    hazard_manager& operator=(const hazard_manager&) = delete;
    hazard_manager(hazard_manager&&) = delete;
    hazard_manager& operator=(hazard_manager&&) = delete;

    ~hazard_manager();

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

    struct alignas(64) hazard_record_t {
        std::array<std::atomic<void*>, hp::max_hazard_count> hps;
        std::atomic<bool> active;
    };

    struct retired_ptr_t {
        void* ptr;
        auto (*deleter)(void*) -> void;
    };
    
    struct tls_data_t {
        hazard_record_t* record;
        std::list<retired_ptr_t> retired_list;
    };

    struct tls_map_t 
    :std::unordered_map<hazard_manager*, tls_data_t>
    {
        ~tls_map_t();
    };

    hazard_record_t* allocate_record();
    void deallocate_record(hazard_record_t* record);

    tls_data_t& local_tls();
    void collect_thread_unretired(std::list<retired_ptr_t>& retireds);

    void scan_retired(std::list<retired_ptr_t>& retired_list);
    void scan_tls_retired();

    std::array<hazard_record_t, hp::max_thread_count> records;
    std::list<retired_ptr_t> g_retired;
    std::mutex g_retired_mutex;
};    



} // namespace seele::structs
