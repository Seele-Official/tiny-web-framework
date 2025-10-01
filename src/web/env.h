#pragma once

#include <filesystem>
#include <vector>

#include "io/io.h"
#include "web/ip.h"
#include "web/response.h"

#include "http/request.h"

namespace web::env {


struct file_router{
    web::response::task operator()(const http::request::msg&){
        return web::response::file(
            this->conten_type,
            this->content
        );
    }

    std::string name{};
    std::string conten_type{};
    io::mmap    content{};
};


namespace detail {
    struct env{
        std::filesystem::path root_path = std::filesystem::current_path() / "www";
        web::ip::v4 listen_addr{};
        std::vector<io::fd> accepter_fds{};
        std::vector<file_router> static_routers{};
    };

    inline env& get_instance() {
        static env instance;
        return instance;
    }
}



web::ip::v4& listen_addr(){
    return detail::get_instance().listen_addr;
}

std::filesystem::path& root_path(){
    return detail::get_instance().root_path;
}

std::vector<io::fd>& accepter_fds(){
    return detail::get_instance().accepter_fds;
}

std::vector<file_router>& static_routers(){
    return detail::get_instance().static_routers;
}


};