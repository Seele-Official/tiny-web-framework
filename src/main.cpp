#include <print>
#include <string_view>
#include "opts.h"
#include "meta.h"
#include "log.h"
#include "server.h"

using namespace seele;

int main(int argc, char* argv[]) {
    // log::logger().set_output_file("web_server.log");
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

    app().run();
    return 0;
}