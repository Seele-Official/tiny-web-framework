#pragma once
#include <cstddef>
#include <vector>

#include "io/io.h"
#include "web/ip.h"
namespace web::loop {

void run();

    
namespace detail {
struct env{
    web::ip::v4 listen_addr{};
    size_t worker_count = 4;
    size_t max_worker_conn = 128;
    std::vector<io::fd> accepter_fds{};
    
    static inline env& get_instance() {
        static env instance;
        return instance;
    }
};
}

namespace env {
inline web::ip::v4& listen_addr(){
    return detail::env::get_instance().listen_addr;
}


inline size_t& worker_count(){
    return detail::env::get_instance().worker_count;
}

inline size_t& max_worker_conn(){
    return detail::env::get_instance().max_worker_conn;
}

inline std::vector<io::fd>& accepter_fds(){
    return detail::env::get_instance().accepter_fds;
}
} // namespace env
}