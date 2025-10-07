#pragma once
#include <cstddef>
#include <filesystem>
#include <string_view>

#include "http/response.h"

#include "web/ip.h"
#include "web/loop.h"
#include "web/routing.h"

namespace web::env {

inline ip::v4& listen_addr(){
    return loop::env::listen_addr();
}

inline size_t& worker_count(){
    return loop::env::worker_count();
}

inline size_t& max_worker_conn(){
    return loop::env::max_worker_conn();
}

inline std::filesystem::path& root_path(){
    return routing::env::root_path();
}

inline routing::function_ref<
    std::string_view(http::response::status_code)
>& error_page_provider(){
    return routing::env::error_page_provider();
}

struct chain {
    chain& set_listen_addr(const web::ip::v4& addr) {
        listen_addr() = addr;
        return *this;
    }

    chain& set_root_path(const std::filesystem::path& path) {
        root_path() = path;
        return *this;
    }

    chain& set_worker_count(size_t count) {
        worker_count() = count;
        return *this;
    }

    chain& set_max_worker_conn(size_t count) {
        max_worker_conn() = count;
        return *this;
    }

    chain& set_error_page_provider(routing::function_ref<
        std::string_view(http::response::status_code)> provider) {
        error_page_provider() = provider;
        return *this;
    }

};


};