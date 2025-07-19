#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <list>
#include <mutex>
#include <unordered_map>
namespace seele::structs {
    namespace mpsc_hp {
        constexpr size_t max_hazard_count = 2;
        constexpr size_t max_thread_count = 64;
        constexpr size_t max_retired_count = 64*16;    
    }

    class mpsc_hazard_manager{
    public:
        mpsc_hazard_manager() = default;
        mpsc_hazard_manager(const mpsc_hazard_manager&) = delete;       
        mpsc_hazard_manager& operator=(const mpsc_hazard_manager&) = delete;
        mpsc_hazard_manager(mpsc_hazard_manager&&) = delete;
        mpsc_hazard_manager& operator=(mpsc_hazard_manager&&) = delete;
        ~mpsc_hazard_manager();

        

        template<size_t index>
            requires (index < mpsc_hp::max_hazard_count)
        void protect(void* ptr){
            this->local_tls()
                ->hps[index].store(ptr, std::memory_order_release);
        }


        template<size_t index>
            requires (index < mpsc_hp::max_hazard_count)
        void clear(){
            this->local_tls()
                ->hps[index].store(nullptr, std::memory_order_release);
        }

        void clear_all();

        template<typename T>
        void retire(T* ptr){
            this->retired.emplace_back(ptr, [](void* p){ delete static_cast<T*>(p); });
            if (this->retired.size() > mpsc_hp::max_retired_count) {
                this->scan_retired();
            }
        }

    private:
        struct alignas(64) hazard_record_t {
            std::array<std::atomic<void*>, mpsc_hp::max_hazard_count> hps;
            std::atomic<bool> active;
        };

        struct retired_ptr_t {
            void* ptr;
            auto (*deleter)(void*) -> void;
        };
        using tls_map_t = std::unordered_map<mpsc_hazard_manager*, hazard_record_t*>; 
        hazard_record_t* allocate_record();
        inline void deallocate_record(hazard_record_t* record);
        void scan_retired();
        hazard_record_t* local_tls();
        

        static thread_local tls_map_t tls_map;

        std::array<hazard_record_t, mpsc_hp::max_thread_count> records;
        std::list<retired_ptr_t> retired;
    };
}