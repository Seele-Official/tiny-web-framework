#pragma once

#include <cstddef>
#include <filesystem>
#include <utility>
#include <vector>

#include "io/io.h"
#include "web/ip.h"
#include "web/response.h"

#include "http/request.h"

namespace web::env {



struct file_router{
    web::response::task operator()(const http::request::msg&){
        return web::response::file(
            this->content_type,
            this->content
        );
    }

    std::string name{};
    std::string content_type{};
    io::mmap    content{};
};

struct file_head_router{
    web::response::task operator()(const http::request::msg&){
        return web::response::file_head(
            this->content_type, 
            this->size
        );
    }
    std::string name{};
    std::string content_type{};
    size_t size{};
};

namespace detail {
    struct env{
        std::filesystem::path root_path = std::filesystem::current_path() / "www";
        web::ip::v4 listen_addr{};
        std::vector<io::fd> accepter_fds{};
        std::vector<std::pair<file_head_router, file_router>> static_routers{};
    };

    inline env& get_instance() {
        static env instance;
        return instance;
    }
}



inline web::ip::v4& listen_addr(){
    return detail::get_instance().listen_addr;
}

inline std::filesystem::path& root_path(){
    return detail::get_instance().root_path;
}

inline std::vector<io::fd>& accepter_fds(){
    return detail::get_instance().accepter_fds;
}

inline std::vector<std::pair<file_head_router, file_router>>& static_routers(){
    return detail::get_instance().static_routers;
}


};