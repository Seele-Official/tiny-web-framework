#pragma once
#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>
#include "meta.h"
#include "coro/sendable_task.h"
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
    using query_t = std::string;
    using body_t = std::string;

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
    };
    

    
    struct req_msg {
        req_line line;
        header_t header;
        body_t body;

        coro::sendable_task<std::optional<std::string_view>, std::string_view> parser();
    };






    enum class status_code : size_t{
        ok = 200,

        bad_request = 400,
        forbidden = 403,
        not_found = 404,
        method_not_allowed = 405,



        internal_server_error = 500,
        not_implemented = 501,
    };
    struct phrase_content{
        status_code code;
        std::string_view str;
    };

    struct phrase_content_map{
        std::array<std::string_view, 900> map;

        consteval phrase_content_map(std::initializer_list<phrase_content> init_list) {
            for (const auto& item : init_list) {
                map[static_cast<size_t>(item.code)] = item.str;
            }
        }

        auto operator[](status_code code) const -> std::string_view {
            return map[static_cast<size_t>(code)];
        }
    };
    
    extern phrase_content_map phrase_contents;

    struct stat_line{
        status_code code;
        template<typename out_t>
        auto format_to(out_t&& out) const {
            return std::format_to(
                std::forward<out_t>(out), 
                "HTTP/1.1 {} {}\r\n", 
                static_cast<size_t>(code), phrase_contents[code]
            );
        }
        std::string to_string() const {
            return std::format(
                "HTTP/1.1 {} {}\r\n", 
                static_cast<size_t>(code), phrase_contents[code]
            );
        }
    };

    class res_msg {
    public:
        res_msg() = default;
        res_msg(status_code code, header_t header, body_t body = "") : 
        stat_l(code), header(std::move(header)), body(std::move(body)) {
            if (!this->body.empty()) {
                this->header.emplace("Content-Length", std::to_string(this->body.size()));
            }

        }
        ~res_msg() = default;
        void set_content_length(size_t size) {
            header.insert_or_assign("Content-Length", std::to_string(size));
        }

        template<typename out_t>
        auto format_to(out_t&& out) const {
            auto it = stat_l.format_to(std::forward<out_t>(out));
            for (const auto& [key, value] : header) {
                it = std::format_to(it, "{}: {}\r\n", key, value);
            }
            it = std::format_to(it, "\r\n{}", body);
            return it;
        }

        std::string to_string() const {
            std::string res;
            res.reserve(256 + body.size());
            res.append(stat_l.to_string());
            for (const auto& [key, value] : header) {
                res.append(std::format("{}: {}\r\n", key, value));
            }
            res.append("\r\n");
            res.append(body);
            return res;
        }
    private:
        stat_line stat_l;
        header_t header;
        body_t body;
    };




    
    struct error_content{
        status_code code;
        std::string_view str;
    };

    struct error_content_map{
        std::array<std::string_view, 900> map;

        consteval error_content_map(std::initializer_list<error_content> init_list) {
            for (const auto& item : init_list) {
                map[static_cast<size_t>(item.code)] = item.str;
            }
        }

        auto operator[](status_code code) const -> std::string_view {
            return map[static_cast<size_t>(code)];
        }
    };


    extern error_content_map error_contents;
    extern std::unordered_map<std::string, std::string> mime_types;

    std::optional<std::string> pct_decode(std::string_view str);
}

