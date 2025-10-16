#include "web/routing.h"
#include "web/ip.h"
#include "web/response.h"
#include "coro/simple_task.h"
#include "http/request.h"

#include <boost/ut.hpp>

#include <chrono>
#include <format>
#include <unordered_map>

namespace {
using namespace std::literals;

using namespace boost::ut;
using namespace web;
using namespace seele;
suite<"static routing"> _ = []{
    "get"_test = []{

        http::request::msg req{
            {
                http::request::method::GET,
                http::request::origin_form{"/hello", ""},
                "HTTP/1.1"
            }
        };

        bool called = false;

        auto router = [&](const http::request::msg& r) -> response::task {
            called = true;
            expect(&req == &r);

            return []() -> response::task {
                co_return 0;
            }();

        };

        routing::get("/hello", router);

        routing::detail::route(req);

        expect(called);
    };

    "post"_test = []{

        http::request::msg req{
            {
                http::request::method::POST,
                http::request::origin_form{"/submit", ""},
                "HTTP/1.1"
            }
        };


        bool called = false;

        auto router = [&](const http::request::msg& r) -> response::task {
            called = true;
            expect(&req == &r);

            return []() -> response::task {
                co_return 0;
            }();

        };

        routing::post("/submit", router);

        routing::detail::route(req);

        expect(called);
    };
};


suite<"dynamic routing"> __ = []{
    "simple route"_test = []{

        routing::dynamic::clear(http::request::method::GET);


        http::request::msg req{
            {
                http::request::method::GET,
                http::request::origin_form{"/seele/home/post/114514", ""},
                "HTTP/1.1"
            }
        };

        bool called = false;

        auto router = [&](const http::request::msg& r, const std::unordered_map<std::string, std::string>& params) -> response::task {
            called = true;
            expect(&req == &r);

            

            expect(params.contains("name") && params.at("name") == "seele");
            expect(params.contains("id") && params.at("id") == "114514");

            return []() -> response::task {
                co_return 0;
            }();

        };

        routing::dynamic::get("/{name}/home/post/{id}", router);

        routing::detail::route(req);

        expect(called);
    };

    "branching routes 1"_test = []{
        routing::dynamic::clear(http::request::method::GET);

        bool called_post = false;
        bool called_space = false;

        auto router_post = [&](const http::request::msg&, const std::unordered_map<std::string, std::string>& params) -> response::task {
            called_post = true;
            expect(params.at("name") == "seele");
            expect(params.at("id") == "114514");
            return []() -> response::task { co_return 0; }();
        };
        auto router_space = [&](const http::request::msg&, const std::unordered_map<std::string, std::string>& params) -> response::task {
            called_space = true;
            expect(params.at("name") == "seele");
            expect(params.at("id") == "2048");
            return []() -> response::task { co_return 0; }();
        };

        routing::dynamic::get("/{name}/home/post/{id}", router_post);
        routing::dynamic::get("/{name}/home/space/{id}", router_space);

        // match post
        {
            http::request::msg req{{ http::request::method::GET, http::request::origin_form{"/seele/home/post/114514", ""}, "HTTP/1.1" }};
            called_post = false; called_space = false;
            routing::detail::route(req);
            expect(called_post && !called_space);
        }

        // match space
        {
            http::request::msg req{{ http::request::method::GET, http::request::origin_form{"/seele/home/space/2048", ""}, "HTTP/1.1" }};
            called_post = false; called_space = false;
            routing::detail::route(req);
            expect(!called_post && called_space);
        }
    };

    "branching routes 2"_test = []{
        routing::dynamic::clear(http::request::method::GET);

        bool called_114514 = false;
        bool called_2048 = false;

        auto router_post = [&](const http::request::msg&, const std::unordered_map<std::string, std::string>& params) -> response::task {
            called_114514 = true;
            expect(params.at("name") == "seele");
            return []() -> response::task { co_return 0; }();
        };
        auto router_space = [&](const http::request::msg&, const std::unordered_map<std::string, std::string>& params) -> response::task {
            called_2048 = true;
            expect(params.at("name") == "seele");
            expect(params.at("id") == "2048");
            return []() -> response::task { co_return 0; }();
        };

        routing::dynamic::get("/{name}/home/post/114514", router_post);
        routing::dynamic::get("/{name}/home/post/{id}", router_space);

        // match 114514
        {
            http::request::msg req{{ http::request::method::GET, http::request::origin_form{"/seele/home/post/114514", ""}, "HTTP/1.1" }};
            called_114514 = false; called_2048 = false;
            routing::detail::route(req);
            expect(called_114514 && !called_2048);
        }

        // match 2048
        {
            http::request::msg req{{ http::request::method::GET, http::request::origin_form{"/seele/home/post/2048", ""}, "HTTP/1.1" }};
            called_114514 = false; called_2048 = false;
            routing::detail::route(req);
            expect(!called_114514 && called_2048);
        }
    };


    "route failure: trailing slash"_test = []{
        routing::dynamic::clear(http::request::method::GET);

        bool called = false;
        auto router_post = [&](const http::request::msg&, const std::unordered_map<std::string, std::string>&) -> response::task {
            called = true; return []() -> response::task { co_return 0; }();
        };
        routing::dynamic::get("/{name}/home/post/{id}", router_post);

        // trailing slash should not match
        http::request::msg req{{ http::request::method::GET, http::request::origin_form{"/seele/home/post/12312/", ""}, "HTTP/1.1" }};
        routing::detail::route(req);
        expect(!called);
    };

    "route failure: no registration"_test = []{
        routing::dynamic::clear(http::request::method::GET);
        bool called = false;
        // not registering any route intentionally
        http::request::msg req{{ http::request::method::GET, http::request::origin_form{"/seele/home/post/1", ""}, "HTTP/1.1" }};
        routing::detail::route(req);
        expect(!called);
    };

};
}