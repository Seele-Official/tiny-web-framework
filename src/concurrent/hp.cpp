#include <memory>
#include <ranges>
#include <algorithm>
#include "concurrent/hp.h"
#include "log.h"
namespace seele::concurrent {

namespace detail {


hazard_recorder::record_t* hazard_recorder::allocate_record(){
    for (auto& record : records) {
        bool expected = false;
        if (record.active.compare_exchange_strong(expected, true, std::memory_order_acquire)) {
            return &record;
        }
    }
    return nullptr; 
}


inline void hazard_recorder::deallocate_record(record_t* record){
    record->active.store(false, std::memory_order_release);
}
void hazard_recorder::collect_unretired(std::vector<retired_ptr_t>& retireds){
    std::lock_guard<std::mutex> lock(g_retired_mutex);
    this->g_retired.insert(this->g_retired.end(), retireds.begin(), retireds.end());
    if (g_retired.size() > hp::max_retired_count) {
        this->scan_retired(g_retired);
    }
    
}

void hazard_recorder::scan_retired(std::vector<retired_ptr_t>& retired_list){

    auto hps = records
        | std::views::filter([](record_t& rec) {
            return rec.active.load(std::memory_order_acquire);
        })
        | std::views::transform([](record_t& rec) -> std::array<std::atomic<void*>, hp::max_hazard_count>& {
            return rec.hps;
        })
        | std::views::join;
    

    auto view = std::ranges::remove_if(
        retired_list, 
        [&hps](retired_ptr_t& rp) {


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
            log::sync::error("retired pointer has no deleter.");
            std::terminate();
        }
    );

    retired_list.erase(view.begin(), view.end());
}

hazard_recorder::~hazard_recorder() {
    std::lock_guard<std::mutex> lock(g_retired_mutex);

    this->scan_retired(g_retired);
    if (g_retired.size() > 0) {
        log::sync::error("Hazard manager still has {} retired pointers after destruction.", g_retired.size());
        for (const auto& [index, record] : std::views::enumerate(this->records)) {
            if (record.active.load(std::memory_order_acquire)) {
                log::sync::error("Hazard record {} is still active.", index);
            }
        }

        std::terminate();
    }
}

}





hazard_manager::tls_t::~tls_t() {
    for (auto& [recorder, data] : this->map) {
        recorder->scan_retired(data.retired_list);
        if(data.retired_list.size() > 0) {
            recorder->collect_unretired(data.retired_list);
        }
        recorder->deallocate_record(data.record);
    }

}


hazard_manager::tls_data_t& hazard_manager::local_tls() {
    static thread_local tls_t tls{};
    auto it = tls.map.find(this->recorder);
    if (it != tls.map.end()) {
        return it->second;
    } else {
        record_t* record = this->recorder->allocate_record();
        // here we assume that allocate_record never returns nullptr
        return tls.map.emplace(
            this->recorder, 
            tls_data_t{record, {}}
        ).first->second;
    }
}



void hazard_manager::scan_tls_retired() {
    auto& r = local_tls().retired_list;
    if (r.size() > hp::max_retired_count) {
        this->recorder->scan_retired(r);
    }
}


void hazard_manager::clear_all(){
    for(auto& hp : this->local_tls().record->hps) {
        hp.store(nullptr, std::memory_order_release);
    }
}





} // namespace seele::concurrent
