#pragma once
#include <cstddef>

#include "io/io.h"
#include "web/ip.h"
#include "coro/simple_task.h"

namespace web::loop {

void run();

coro::simple_task reg_stop_signal(int signo = SIGINT);


namespace env {
inline web::ip::v4& listen_addr(){
    static web::ip::v4 listen_addr{};
    return listen_addr;
}


inline size_t& worker_count(){
    static size_t worker_count = 4;
    return worker_count;
}

inline size_t& max_worker_conn(){
    static size_t max_worker_conn = 128;
    return max_worker_conn;
}
} // namespace env
}