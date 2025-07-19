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


class TinyApp{
public:
    std::expected<handler_response, http::error_code> operator()(const http::query_t& query, const http::header_t& header) {
        std::println("Received GET request for /tiny_app with query:");
        if (query) {
            if (auto parsed_query = parse_query(*query); parsed_query.has_value()) {
                for (const auto& [key, value] : parsed_query.value()) {
                    std::println("  {}: {}", key, value);
                }
            } else {
                std::println("Invalid query format");
                return std::unexpected(http::error_code::bad_request);
            }
        } else {
            std::println("No query");
        }

        std::println("Headers:");
        for (const auto& [key, value] : header) {
            std::println("  {}: {}", key, value);
        }

        
        return http_file_ctx::make(
            {
                {200, "OK"},
                {
                    {"Content-Type", "application/json"},
                },
                std::nullopt
            },
            f.data,
            f.size
        );
    };
    mmap_wrapper f;

    TinyApp(int fd, size_t size): f(size, PROT_READ, MAP_SHARED, fd) {}
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
    fd_wrapper file_fd = open("response.json", O_RDONLY);
    if (!file_fd.is_valid()) {
        std::println("Failed to open response.json");
        return 0;
    }
    size_t file_size = get_file_size(file_fd);

    TinyApp tiny_app(file_fd, file_size);
    app().GET("/tiny_app.so", tiny_app);

    app().run();
    return 0;
}