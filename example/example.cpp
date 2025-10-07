#include "web/env.h"
#include "web/loop.h"
#include "web/response.h"
#include "web/routing.h"
#include "json/json.h"
#include <string_view>


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

    web::env::chain()
        .set_listen_addr(web::ip::v4::from_string("127.0.0.1:8080"))
        .set_root_path("www")
        .set_max_worker_conn(128)
        .set_worker_count(4);


    web::routing::get("/hello", [](const http::request::msg&) -> web::response::task {
        return web::response::msg(
            make_success_msg("text/plain", "Hello, World!")
        );
    });


    web::routing::dynamic::get("/user/{id}", [](const http::request::msg&, const std::unordered_map<std::string, std::string>& params) -> web::response::task {
        return web::response::msg(
            make_success_msg(
                "text/plain", 
                std::format("User ID: {}", params.at("id"))
            )
        );
    });


    web::routing::get("/data", [](const http::request::msg&) -> web::response::task {
        Json::object obj;
        obj["message"] = "Hello, JSON!";
        obj["value"] = 42;
        obj["array"] = Json::array{1, 2, 3};
        
        return web::response::msg(
            make_success_msg("application/json", std::format("{}", obj))
        );
    });

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

    web::loop::run();
}