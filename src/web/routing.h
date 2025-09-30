#pragma once
#include <string_view>
#include <vector>
#include <ranges>
#include "function_ref.h"

#include "web/ip.h"
#include "web/response.h"
#include "http/request.h"



namespace web::routing {
using namespace http;
using namespace seele;

using route_result = web::response::task;

namespace detail {
    route_result route(const request::msg& req);
}

using router = function_ref<route_result(const request::msg&)>;





void get(std::string_view path, router r);

void post(std::string_view path, router r);

namespace dynamic{
using router = function_ref<
    route_result(
        const request::msg&, 
        const std::unordered_map<std::string, std::string>&
    )
>;

// TODO: implement compile-time path template parsing
struct path_template{
    struct part{
        enum {
            static_part,
            param_part
        } type;
        std::string_view str;
    };


    static auto make(std::string_view sv){
        return sv 
            | std::views::split('/')
            | std::views::transform([](auto &&rng) {
                std::string_view sv(&*rng.begin(), std::ranges::distance(rng));
                if (sv.starts_with('{') && sv.ends_with('}')) {
                    return part{part::param_part, sv.substr(1, sv.size() - 2)};
                } else {
                    return part{part::static_part, sv};
                }
            })
            | std::ranges::to<std::vector<part>>();
    };


    path_template(std::string_view sv) : parts{make(sv)}{}

    template<typename... args_t>
        requires std::is_constructible_v<std::string_view, args_t...>
    path_template(args_t&&... args) : parts(make(std::string_view{std::forward<args_t>(args)...})) {}


    std::vector<part> parts;
};

void get(path_template path, router r);
void post(path_template path, router r);


// Only for testing purpose
void clear(request::method m);

};


}