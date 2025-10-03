#include "web/env.h"
#include "web/loop.h"
#include "web/response.h"
#include "web/routing.h"

int main(){
    web::env::listen_addr() = web::ip::v4::from_string("127.0.0.1:8080");

    web::routing::get("/hello", [](const http::request::msg&) -> web::response::task {
        return web::response::msg({
            {http::response::status_code::ok},
            {
                {"Content-Type", "text/plain"},
                {"Content-Length", "13"}
            },
            "Hello, World!"
        });
    });


    web::routing::dynamic::get("/user/{id}", [](const http::request::msg&, const std::unordered_map<std::string, std::string>& params) -> web::response::task {
        auto msg = std::format("User ID: {}", params.at("id"));

        return web::response::msg({
            {http::response::status_code::ok},
            {
                {"Content-Type", "text/plain"},
                {"Content-Length", std::to_string(msg.size())}
            },
            msg
        });
    });

    web::loop::run();
    return 0;
}