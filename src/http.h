#pragma once
#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include "meta.h"
#include "coro/co_task.h"
#include "coro/task.h"



namespace http {
    using namespace seele;       
    
    enum method_t {
        GET,
        POST,
        PUT,
        DELETE,
        HEAD,
        OPTIONS,
        PATCH,
        CONNECT,
        TRACE
    };     


    struct origin_form{
        std::string path;
        std::optional<std::string> query;
    };
    struct absolute_form{};
    struct authority_form{};
    struct asterisk_form{};

    using request_target_t = std::variant<
        origin_form, absolute_form, authority_form, asterisk_form
    >;

    struct req_line {

        method_t method;
        request_target_t target;
        std::string version;
        
        template<typename out_t>
        auto format_to(out_t&& out) const {
            return std::format_to(std::forward<out_t>(out), "{} {} {}\r\n", 
                meta::enum_to_string(method), target, version);
        }

    };
    
    struct parse_res;
    struct req_msg {

        req_line line;
        std::unordered_map<std::string, std::string> fields;
        std::optional<std::string> body;


        static coro::co_task<std::optional<parse_res>, std::string_view> parser();
    };

    struct parse_res{
        req_msg msg;
        std::optional<std::string_view> remain; 
    };


    struct stat_line{
        size_t status_code;
        std::string version;
        std::string reason_phrase;
        stat_line(size_t status_code, std::string reason_phrase) :
            status_code(status_code),
            version("HTTP/1.1"),
            reason_phrase(std::move(reason_phrase)) {}
        template<typename out_t>
        auto format_to(out_t&& out) const {
            return std::format_to(std::forward<out_t>(out), "{} {} {}\r\n", version, status_code, reason_phrase);
        }
    };

    struct res_msg {
        stat_line stat_l;
        std::unordered_map<std::string, std::string> fields;
        std::optional<std::string> body;
        
        template<typename out_t>
        auto format_to(out_t&& out) const {
            auto it = stat_l.format_to(std::forward<out_t>(out));
            for (const auto& [key, value] : fields) {
                it = std::format_to(it, "{}: {}\r\n", key, value);
            }
            it = std::format_to(it, "\r\n");
            if (body) {
                it = std::format_to(it, "{}", *body);
            }
            return it;
        }
    };



    enum class error_code : size_t{
        bad_request = 400,
        forbidden = 403,
        not_found = 404,
        method_not_allowed = 405,



        internal_server_error = 500,
        not_implemented = 501,
    };

    struct error_content{
        error_code code;
        std::string_view str;
    };

    struct error_content_map{
        std::array<std::string_view, 900> map;

        consteval error_content_map(std::initializer_list<error_content> init_list) {
            for (const auto& item : init_list) {
                map[static_cast<size_t>(item.code)] = item.str;
            }
        }

        auto operator[](error_code code) const -> std::string_view {
            return map[static_cast<size_t>(code)];
        }
    };


    extern error_content_map error_contents;
    extern std::unordered_map<std::string, std::string> mime_types;
}

