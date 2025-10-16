#include "log.h"
#include "web/env.h"
#include "web/loop.h"
#include "web/response.h"
#include "web/routing.h"
#include "json/json.h"
#include "io/awaiter.h"
#include <string_view>
#include <vector>
#include <chrono>


http::response::msg make_success_msg(std::string&& content_type, std::string&& body) {
    return {
        {http::response::status_code::ok},
        {
            {"Content-Type", std::move(content_type)},
            {"Content-Length", std::to_string(body.size())}
        },
        std::move(body)
    };
}

int main(){
    using namespace seele;
    // Configure the logging system
    log::set_output_file("server.log");


    // Configure the web server environment
    web::env::chain()
        .set_listen_addr(web::ip::v4::from_string("127.0.0.1:8080"))
        .set_root_path("www")
        .set_max_worker_conn(128)
        .set_worker_count(16)
        /* Options for custom error page provider:
         .set_error_page_provider([](http::response::status_code) -> std::string_view {})
        */
    ;

    // Simple static route
    web::routing::get("/hello", [](const http::request::msg&) -> web::response::task {
        return web::response::msg(
            make_success_msg("text/plain", "Hello, World!")
        );
    });

    // Dynamic route with parameter
    web::routing::dynamic::get("/user/{id}", [](const http::request::msg&, const std::unordered_map<std::string, std::string>& params) -> web::response::task {
        return web::response::msg(
            make_success_msg(
                "text/plain", 
                std::format("User ID: {}", params.at("id"))
            )
        );
    });

    // JSON response route
    web::routing::get("/data", [](const http::request::msg&) -> web::response::task {
        Json::object obj;
        obj["message"] = "Hello, JSON!";
        obj["value"] = 42;
        obj["array"] = Json::array{1, 2, 3};
        
        return web::response::msg(
            make_success_msg("application/json", std::format("{}", obj))
        );
    });

    // POST route handling JSON body
    web::routing::post("/submit", [](const http::request::msg& req) -> web::response::task {
        auto body = req.body;

        auto json = Json::parse(body);
        if (!json) {
            return web::response::error(http::response::status_code::bad_request);
        } else {
            auto object = json->as<Json::object>();
            if (!object) {
                return web::response::error(http::response::status_code::bad_request);
            }
            object->get().emplace("status", "received");
            return web::response::msg(
                make_success_msg(
                    "application/json", 
                    std::format("{}", object->get())
                )
            );
        }
    });

    // Async
    web::routing::get("/async", [](const http::request::msg&) -> web::response::task {
        using namespace std::literals;

        // Simulate async operation with timeout
        co_await io::awaiter::sleep{1s};

        // Optional: get settings like fd, client_addr, timeout
        auto _ = co_await web::response::task::get_settings{};
        
        // Return response
        co_return co_await web::response::msg(
            make_success_msg("text/plain", "This is an async response!")
        );
    });

    // Start the event loop
    web::loop::run();
}