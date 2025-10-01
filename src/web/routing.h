#pragma once
#include <exception>
#include <string>
#include <string_view>
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
class path_template{
public:
    struct part{
        enum {
            static_part,
            param_part
        } type;
        std::string_view str;
    };

    template<typename... args_t>
        requires std::is_constructible_v<std::string_view, args_t...>
    consteval path_template(args_t&&... args): sv(std::forward<args_t>(args)...) {

        auto view = this->sv 
            | std::views::split('/')
            | std::views::transform([](const auto &rng) {
                return std::string_view(rng);
            });

        for (auto part : view) {
            if (part.starts_with('{') && part.ends_with('}')) {
                for (auto c : part.substr(1, part.size() - 2)) {
                    if (!(('a' <= c && c <= 'z') || ('0' <= c && c <= '9') || c == '_')) {
                        std::terminate();// throw std::invalid_argument("Invalid character in parameter name");
                    }
                }
            }
        }
    }

    auto parts(){
        return this->sv 
            | std::views::split('/')
            | std::views::transform([](const auto &rng) {
                std::string_view sv(rng);
                if (sv.starts_with('{') && sv.ends_with('}')) {
                    return part{part::param_part, sv.substr(1, sv.size() - 2)};
                } else {
                    return part{part::static_part, sv};
                }
            });
    }
    
private:
    std::string_view sv{};
};

void get(path_template path, router r);
void post(path_template path, router r);


// Only for testing purpose
void clear(request::method m);

};


}