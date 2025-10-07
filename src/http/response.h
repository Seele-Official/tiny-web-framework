#pragma once
#include <cstddef>
#include <string_view>
#include <string>
#include <unordered_map>
#include <utility>
#include <format>

namespace http::response {
using header = std::unordered_map<std::string, std::string>;
using body = std::string;
enum class status_code : size_t {
    ok =                        200,

    bad_request =               400,
    forbidden =                 403,
    not_found =                 404,
    method_not_allowed =        405,

    internal_server_error =     500,
    not_implemented =           501,
    
};

std::string_view status_code_to_string(status_code code);

struct status_line{
    template<typename out_t>
    auto format_to(out_t&& out) const {
        return std::format_to(
            std::forward<out_t>(out), 
            "HTTP/1.1 {} {}\r\n", 
            static_cast<size_t>(code), status_code_to_string(code)
        );
    }

    status_code code;
};


struct msg {
    template<typename out_t>
    auto format_to(out_t&& out) const {
        auto it = status_line.format_to(std::forward<out_t>(out));
        for (const auto& [key, value] : header) {
            it = std::format_to(it, "{}: {}\r\n", key, value);
        }
        it = std::format_to(it, "\r\n{}", body);
        return it;
    }


    response::status_line status_line{};
    response::header      header{};
    response::body        body{};
};

}// namespace http::response
