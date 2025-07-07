#include <print>
#include <csignal>
#include <string_view>
#include <filesystem>
#include "coro/task.h"
#include "net/ipv4.h"
#include "opts.h"
#include "meta.h"
#include "log.h"
#include "io_ctx.h"


using namespace seele;

coro::task server_loop(net::ipv4);

extern std::string_view default_path;
int main(int argc, char* argv[]) {
    log::logger().set_enable(true);
    // log::logger().set_output_file("web_server.log");
    auto opts = opts::make_opts(
        opts::ruler::req_arg("--address", "-a"),
        opts::ruler::req_arg("--path", "-p")
    );

    auto res = opts.parse(argc, argv);

    net::ipv4 addr{};
    for(auto&& opt : res) {
        if (opt.has_value()){
            meta::visit_var(opt.value(), 
                [&](opts::req_arg& arg){
                    if (arg.long_name == "--address"){
                        auto parsed = net::parse_addr(arg.value);
                        if (parsed.has_value()) {
                            addr = parsed.value();
                            std::println("Parsed address: {}", addr.toString());
                        } else {
                            std::println("Failed to parse address: {}", parsed.error());
                            std::terminate();
                        }
                    } else if (arg.long_name == "--path") {
                        if (!std::filesystem::exists(arg.value) || !std::filesystem::is_directory(arg.value) || !std::filesystem::path(arg.value).is_absolute()) {
                            std::println("Invalid path: {}. It must be an absolute path to an existing directory.", arg.value);
                            std::terminate();
                        }
                        default_path = arg.value;
                    } else {
                        std::println("Unknown option: {}", arg.long_name);
                        std::terminate();
                    }
                },
                [](auto&&){}
            );

        }
    }

    if (!addr.is_valid() || default_path.empty()) {
        std::println("Usage: {} --address <ip:port> --path <directory>", argv[0]);
        std::println("Example: {} --address 127.0.0.1:8080 --path /var/www/html", argv[0]);
        std::terminate();
    }

    server_loop(addr);
    std::signal(SIGINT, [](int) {
        std::println("Received SIGINT, stopping server...");
        io_ctx::get_instance().request_stop();
    });
    io_ctx::get_instance().run();
    return 0;
}