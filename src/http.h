#pragma once
#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
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
    using header_t = std::unordered_map<std::string, std::string>;
    using query_t = std::optional<std::string>;

    using body_t = std::optional<std::string>;
    struct origin_form{
        std::string path;
        query_t query;
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
    
    struct parse_ret;

    struct req_msg {
        req_line line;
        header_t header;
        body_t body;


        static coro::co_task<std::optional<parse_ret>, std::string_view> parser();
    };

    struct parse_ret{
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
        std::string to_string() const {
            return std::format("{} {} {}\r\n", version, status_code, reason_phrase);
        }
    };

    struct res_msg {
        stat_line stat_l;
        header_t header;
        body_t body;

        void refresh_content_length() {
            if (body.has_value()) {
                header.insert_or_assign("Content-Length", std::to_string(body->size()));
            } else {
                header.erase("Content-Length");
            }
        }
        void refresh_content_length(size_t size) {
            header.insert_or_assign("Content-Length", std::to_string(size));
        }

        template<typename out_t>
        auto format_to(out_t&& out) const {
            auto it = stat_l.format_to(std::forward<out_t>(out));
            for (const auto& [key, value] : header) {
                it = std::format_to(it, "{}: {}\r\n", key, value);
            }
            it = std::format_to(it, "\r\n");
            if (body) {
                it = std::format_to(it, "{}", *body);
            }
            return it;
        }

        std::string to_string() const {
            std::string res;
            res.reserve(256 + body.value_or("").size());
            res.append(stat_l.to_string());
            for (const auto& [key, value] : header) {
                res.append(std::format("{}: {}\r\n", key, value));
            }
            res.append("\r\n");
            if (body) {
                res.append(*body);
            }
            return res;
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

    std::optional<std::string> pct_decode(std::string_view str);
}

