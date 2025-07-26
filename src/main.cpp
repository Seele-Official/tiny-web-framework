#include <cmath>
#include <cstddef>
#include <expected>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <unordered_map>
#include "http.h"
#include "io.h"
#include "opts.h"
#include "meta.h"
#include "log.h"
#include "server.h"
#include "basic.h"

using namespace seele;

using str_map = std::unordered_map<std::string, std::string>;



std::optional<str_map> parse_query(const std::string& query) {
    str_map result;
    auto parts = basic::split_string_view(query, '&');
    for (const auto& part : parts) {
        auto key_value = basic::split_string_view(part, '=');
        if (key_value.size() == 2) {
            // because server had checked if key_value[0] and key_value[1] are valid pct-encoded strings
            result.emplace(http::pct_decode(key_value[0]).value(), http::pct_decode(key_value[1]).value());
        } else {
            return std::nullopt;
        }
    }
    return result;
}


class tiny_get_app{
public:
    web::handler_response operator()(const http::query_t& query, const http::header_t& header) {
        std::println("Received GET request for /tiny_app with query:");
        if (!query.empty()) {
            if (auto parsed_query = parse_query(query); parsed_query.has_value()) {
                for (const auto& [key, value] : parsed_query.value()) {
                    std::println("  {}: {}", key, value);
                }
            } else {
                std::println("Invalid query format");
                return web::send_http_error(http::status_code::bad_request);
            }
        } else {
            std::println("No query");
        }

        std::println("Headers:");
        for (const auto& [key, value] : header) {
            std::println("  {}: {}", key, value);
        }

        return web::send_msg(
            http::res_msg{
                http::status_code::ok,
                {
                    {"Content-Type", "text/plain; charset=utf-8"},
                    {"X-Content-Type-Options", "nosniff"},
                },
                "Hello from tiny_get_app!"
            }
        );
        
    };

    tiny_get_app(){}
};



int main(int argc, char* argv[]) {
    log::logger().set_output_file("web_server.log");
    auto opts = opts::make_opts(
        opts::ruler::req_arg("--address", "-a"),
        opts::ruler::req_arg("--path", "-p")
    );

    auto res = opts.parse(argc, argv);
    for(auto&& opt : res) {
        if (opt.has_value()){
            meta::visit_var(opt.value(), 
                [&](opts::req_arg& arg){
                    if (arg.long_name == "--address"){
                        app().set_addr(arg.value);
                    } else if (arg.long_name == "--path") {
                        app().set_root_path(arg.value);
                    } else {
                        std::println("Unknown option: {}", arg.long_name);
                        std::terminate();
                    }
                },
                [](auto&&){}
            );
        }
    }
    tiny_get_app tiny_app;
    
    app().GET("/tiny_app", tiny_app);

    app().run();
    return 0;
}