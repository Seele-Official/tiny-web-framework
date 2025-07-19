#include <ranges>
#include <algorithm>
#include "structs/mpsc_hp.h"
#include "log.h"
namespace seele::structs {
    
    thread_local mpsc_hazard_manager::tls_map_t mpsc_hazard_manager::tls_map{};
    mpsc_hazard_manager::~mpsc_hazard_manager() {            
        this->scan_retired();
        if (this->retired.size() > 0) {
            log::sync().error("Hazard manager still has {} retired pointers after destruction.", this->retired.size());
            for (const auto& [index, record] : std::views::enumerate(this->records)) {
                if (record.active.load(std::memory_order_acquire)) {
                    log::sync().error("Hazard record {} is still active.", index);
                }
            }

            std::terminate();
        }
    }
    mpsc_hazard_manager::hazard_record_t* mpsc_hazard_manager::allocate_record(){
        for (auto& record : records) {
            bool expected = false;
            if (record.active.compare_exchange_strong(expected, true, std::memory_order_acquire)) {
                return &record;
            }
        }
        return nullptr; 
    }


    inline void mpsc_hazard_manager::deallocate_record(hazard_record_t* record){
        record->active.store(false, std::memory_order_release);
    }


    mpsc_hazard_manager::hazard_record_t* mpsc_hazard_manager::local_tls() {
        auto it = tls_map.find(this);
        if (it != tls_map.end()) {
            return it->second;
        } else {
            hazard_record_t* record = allocate_record();
            // here we assume that allocate_record never returns nullptr
            return tls_map.emplace(this, record).first->second;
        }
    }

    void mpsc_hazard_manager::scan_retired(){

        auto hps = records
            | std::views::filter([](hazard_record_t& rec) {
                return rec.active.load(std::memory_order_acquire);
            })
            | std::views::transform([](hazard_record_t& rec) -> std::array<std::atomic<void*>, mpsc_hp::max_hazard_count>& {
                return rec.hps;
            })
            | std::views::join;
        


        this->retired.remove_if([&hps](retired_ptr_t& rp) {


            auto& [ptr, deleter] = rp;
            if (std::ranges::any_of(hps, [&ptr](std::atomic<void*>& hazard) {
                    return hazard.load(std::memory_order_acquire) == ptr;
                })) {
                // If the retired pointer is still in use, we need to keep it
                return false;
            }
            // Otherwise, we can safely delete it
            if (deleter) {
                deleter(ptr);
                return true; // Remove from the list
            }
            log::sync().error("retired pointer has no deleter.");
            std::terminate();
        });
    }
}