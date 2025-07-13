#include <cmath>
#include <expected>
#include <print>
#include <string_view>
#include "http.h"
#include "opts.h"
#include "meta.h"
#include "log.h"
#include "server.h"

using namespace seele;

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
    auto tiny_app = [](const http::query_t& query, const http::header_t& header) -> std::expected<handler_response, http::error_code> {
        // Handle the GET request for /test
        std::println("Received GET request for /test with query:");
        if (query) {
            auto query_parts = http::split_string_view(*query, '&');
            for (const auto& part : query_parts) {
                std::println("  {}", part);
                auto key_value = http::split_string_view(part, '=');
                if (key_value.size() == 2) {
                    auto key = http::pct_decode(key_value[0]).value_or("error");
                    auto value = http::pct_decode(key_value[1]).value_or("error");
                    std::println("    Key: {}, Value: {}", key, value);
                }
            }
        } else {
            std::println("  No query");
        }


        std::println("Headers:");
        for (const auto& [key, value] : header) {
            std::println("  {}: {}", key, value);
        }

        http::res_msg res{
            {200, "OK"},
            {
                {"Content-Type", "text/plain"},
                {"X-Content-Type-Options", "nosniff"}
            },
            "Hello, world! This is a response from the tiny app."
        };
        res.reset_content_length();
        return res;
    };

    app().GET("/tiny_app.so", tiny_app);

    app().run();
    return 0;
}