#pragma once

#include <filesystem>
#include <vector>


#include "web/ip.h"


namespace web::env {

namespace detail {
    struct env{
        std::filesystem::path root_path = std::filesystem::current_path() / "www";
        web::ip::v4 listen_addr{};
        // std::vector<fd_wrapper> accepter_fd_list{};
    };

    inline env& get_instance() {
        static env instance;
        return instance;
    }
}

web::ip::v4 listen_addr(){
    return detail::get_instance().listen_addr;
}

std::filesystem::path root_path(){
    return detail::get_instance().root_path;
}

};