#pragma once
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include "meta.h"
#include "coro/co_task.h"
#include "coro/task.h"
using namespace seele;


namespace http {
    struct parse_res;   
     
    struct req_line {
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
        method_t method;
        std::string uri;
        std::string version;
        std::string toString(){
            return std::format(
                "{} {} {}\r\n",
                meta::enum_to_string(method),
                uri,
                version
            );
        }

    };
    struct req_msg {

        req_line req_l;
        std::unordered_map<std::string, std::string> fields;
        std::optional<std::string> body;


        static coro::co_task<std::optional<parse_res>, std::string_view> parser();
    };

    struct parse_res{
        req_msg msg;
        std::optional<std::string_view> remain; 
    };


    struct stat_line{
        int64_t status_code;
        std::string version;
        std::string reason_phrase;
        std::string toString() const {
            return std::format("{} {} {}\r\n", version, status_code, reason_phrase);
        }
    };

    struct res_msg {
        stat_line stat_l;
        std::unordered_map<std::string, std::string> fields;
        std::optional<std::string> body;

        std::string toString() const {
            std::string res = stat_l.toString();
            for (const auto& [key, value] : fields) {
                res += std::format("{}: {}\r\n", key, value);
            }
            res += "\r\n";
            if (body) {
                res += *body;
            }
            return res;
        }
    };


}

