#pragma once
#include <exception>
#include <string>
#include <string_view>
#include <ranges>
#include <type_traits>
#include <functional>

#include "web/response.h"
#include "http/request.h"



namespace web::routing {
    
using namespace http;

template <typename signature_t>
class function_ref {
    static_assert(false, "signature_t must be a function type");
};

template <typename R, typename... args_t>
class function_ref<R(args_t...)> {
public:
    using signature_t = R(args_t...);
    
    using erased_t = union {
        void* ctx;
        signature_t* fn;
    };

    function_ref(const function_ref&) = default;
    function_ref(function_ref&&) = default;

    function_ref& operator=(const function_ref&) = default;
    function_ref& operator=(function_ref&&) = default;


    template<typename invocable_t>
        requires std::is_invocable_r_v<R, invocable_t, args_t...> && (!std::is_convertible_v<invocable_t, signature_t*>)
                && (!std::is_same_v<function_ref<R(args_t...)>, invocable_t>)
    constexpr function_ref(invocable_t& inv)
      : proxy{[](erased_t c, args_t... args) -> R {
            return std::invoke(*static_cast<invocable_t*>(c.ctx), static_cast<args_t>(args)...);
        }},
        ctx{.ctx = static_cast<void*>(&inv)}
    {}

    template<typename invocable_t>
        requires std::is_invocable_r_v<R, invocable_t, args_t...> && std::is_convertible_v<invocable_t, signature_t*>
    constexpr function_ref(const invocable_t& inv)
      : proxy{[](erased_t c, args_t... args) -> R {
            return std::invoke(c.fn, static_cast<args_t>(args)...);
        }},
        ctx{.fn = inv}
    {}

    constexpr R operator()(args_t... args) const { return proxy(ctx, static_cast<args_t>(args)...); }

private:
    R (*proxy)(erased_t, args_t...);
    erased_t ctx;
};


using route_result = web::response::task;

namespace detail {
route_result route(const request::msg& req);

struct file_router{
    response::task operator()(const http::request::msg&){
        return response::file(
            this->content_type,
            this->content
        );
    }

    std::string name{};
    std::string content_type{};
    io::mmap    content{};
};

struct file_head_router{
    web::response::task operator()(const http::request::msg&){
        return web::response::file_head(
            this->content_type, 
            this->size
        );
    }
    std::string name{};
    std::string content_type{};
    size_t size{};
};

struct env{
    std::filesystem::path root_path = std::filesystem::current_path() / "www";
    std::vector<std::pair<file_head_router, file_router>> static_routers{};
    
    function_ref<
        std::string_view(http::response::status_code)
    > error_page_provider = [](http::response::status_code code) -> std::string_view {
        return http::response::status_code_to_string(code);
    };

    static inline env& get_instance() {
        static env instance;
        return instance;
    }
};


} // namespace detail

namespace env {

using file_router = detail::file_router;
using file_head_router = detail::file_head_router;

inline std::filesystem::path& root_path(){
    return detail::env::get_instance().root_path;
}
inline std::vector<std::pair<file_head_router, file_router>>& static_routers(){
    return detail::env::get_instance().static_routers;
}
inline function_ref<
    std::string_view(http::response::status_code)
>& error_page_provider(){
    return detail::env::get_instance().error_page_provider;
}
} // namespace env


using router = function_ref<route_result(const request::msg&)>;


void get(std::string_view path, router r);

void head(std::string_view path, router r);

void post(std::string_view path, router r);

void put(std::string_view path, router r);

void del(std::string_view path, router r);

namespace dynamic{
using router = function_ref<
    route_result(
        const request::msg&, 
        const std::unordered_map<std::string, std::string>&
    )
>;


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

void head(path_template path, router r);

void put(path_template path, router r);

void del(path_template path, router r);

// Only for testing purpose
void clear(request::method m);

} // namespace dynamic


}