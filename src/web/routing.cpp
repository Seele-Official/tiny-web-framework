#include <algorithm>
#include <exception>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <sys/stat.h>
#include <unordered_map>
#include <utility>
#include <vector>

#include "web/routing.h"
#include "http/request.h"




namespace web::routing {

namespace detail {
using router_map = std::unordered_map<std::string, router>;


template<typename request::method>
struct router_map_dispatcher{
    static router_map& get(){
        static router_map map{};
        return map;
    }
};


template<>
struct router_map_dispatcher<request::method::OPTIONS>{
    static router_map& get(){
        static router_map map{
        {"*", 
                [](const request::msg&) -> route_result {
                    return web::response::msg({
                        {http::response::status_code::ok},
                        {
                            {"Content-Length", "0"},
                            {"Allow", "GET, HEAD, POST, PUT, DELETE, OPTIONS"}
                        },
                        ""
                    });
                }
            }
        };
        return map;
    }
};

//TODO: Implement sepcialization for TRACE and CONNECT 

router_map& static_router_map(request::method m){
    switch (m) {
        case request::method::GET:
            return router_map_dispatcher<request::method::GET>::get();
        case request::method::HEAD:
            return router_map_dispatcher<request::method::HEAD>::get();
        case request::method::POST:
            return router_map_dispatcher<request::method::POST>::get();
        case request::method::PUT:
            return router_map_dispatcher<request::method::PUT>::get();
        case request::method::DELETE:
            return router_map_dispatcher<request::method::DELETE>::get();
        case request::method::OPTIONS:
            return router_map_dispatcher<request::method::OPTIONS>::get();
        case request::method::TRACE:
            return router_map_dispatcher<request::method::TRACE>::get();
        case request::method::CONNECT:
            return router_map_dispatcher<request::method::CONNECT>::get();
    }
    std::terminate();
}

class router_radix_tree {
public:
    struct node;

    struct param_node{
        std::string name;
        std::unique_ptr<node> child;
    };

    struct node {
        std::unordered_map<
            std::string, 
            std::unique_ptr<node>
        > children{};

        std::optional<dynamic::router> handler{std::nullopt};
        std::optional<param_node> param_child{std::nullopt};
    };

    router_radix_tree() = default;
    ~router_radix_tree() = default;

    void insert(dynamic::path_template path, dynamic::router r) {
        auto curr = &this->root;

        for (auto part : path.parts()) {
            switch (part.type) {
                case dynamic::path_template::part::static_part: {
                    if (auto it = curr->children.find(std::string(part.str)); it != curr->children.end()) {
                        curr = it->second.get();
                    } else {
                        curr = curr->children.emplace(
                            part.str, std::make_unique<node>()
                        ).first->second.get();
                    }
                    break;
                }
                case dynamic::path_template::part::param_part: {
                    if (!curr->param_child) {
                        curr->param_child = param_node{
                            .name = std::string(part.str),
                            .child = std::make_unique<node>()
                        };
                    }
                    curr = curr->param_child->child.get();
                    break;
                }
            
            }
        }

        curr->handler = r;
    }

    template<std::ranges::input_range R>
        requires std::same_as<
            std::ranges::range_value_t<R>, 
            std::string_view
        >
    std::optional<route_result> route(R&& path_parts, const request::msg& req) {
        auto curr = &this->root;
        std::unordered_map<std::string, std::string> params;

        for (auto part : path_parts) {
            if (auto it = curr->children.find(std::string(part)); it != curr->children.end()) {
                curr = it->second.get();
            } else if (curr->param_child) {
                params[curr->param_child->name] = std::string(part);
                curr = curr->param_child->child.get();
            } else {
                return std::nullopt;
            }
        }
        if (curr->handler) {
            return (*curr->handler)(req, params);
        } else {
            return std::nullopt;
        }
    }

    // Only for testing purpose
    void clear() {
        this->root = node{};
    }


private:
    node root{};
};



template<typename request::method>
struct dynamic_router_tree_dispatcher{
    static router_radix_tree& get(){
        static router_radix_tree tree{};
        return tree;
    }
};

router_radix_tree& dynamic_router_tree(request::method m){
    switch (m) {
        case request::method::GET:
            return dynamic_router_tree_dispatcher<request::method::GET>::get();
        case request::method::HEAD:
            return dynamic_router_tree_dispatcher<request::method::HEAD>::get();
        case request::method::POST:
            return dynamic_router_tree_dispatcher<request::method::POST>::get();
        case request::method::PUT:
            return dynamic_router_tree_dispatcher<request::method::PUT>::get();
        case request::method::DELETE:
            return dynamic_router_tree_dispatcher<request::method::DELETE>::get();
        case request::method::OPTIONS:
            return dynamic_router_tree_dispatcher<request::method::OPTIONS>::get();
        case request::method::TRACE:
            return dynamic_router_tree_dispatcher<request::method::TRACE>::get();
        case request::method::CONNECT:
            return dynamic_router_tree_dispatcher<request::method::CONNECT>::get();
    }
    std::terminate();
}



route_result route(const request::msg& req){
    auto& [method, target, version] = req.line;


    if (auto origin = std::get_if<http::request::origin_form>(&target)) {
        // Notice: we assume that the path is already percent-decoded
        auto& map = static_router_map(req.line.method);
        auto& tree = dynamic_router_tree(req.line.method);

        if (auto it = map.find(origin->path); it != map.end()) {
            return it->second(req);
        } else if (auto ret = 
                tree.route(
                        std::string_view{origin->path}
                            | std::views::split('/')
                            | std::views::transform([](auto &&rng) {
                                return std::string_view(rng);
                            }),
                        req
                    )
            ; ret) {
            
            return std::move(ret.value());
        } else {

            return web::response::error(http::response::status_code::not_found);
        }

    }

    else if (auto asterisk = std::get_if<http::request::asterisk_form>(&target); asterisk) {
        auto& map = static_router_map(req.line.method);
        if (auto it = map.find("*"); it != map.end()) {
            return it->second(req);
        } else {
            return web::response::error(http::response::status_code::not_found);
        }
    }

    // TODO: implement absolute_form and authority_form
    return web::response::error(http::response::status_code::bad_request);
}
}


namespace dynamic {

void get(path_template path, router r){
    detail::dynamic_router_tree(request::method::GET).insert(path, r);
}

void post(path_template path, router r){
    detail::dynamic_router_tree(request::method::POST).insert(path, r);
}

void put(path_template path, router r){
    detail::dynamic_router_tree(request::method::PUT).insert(path, r);
}

void del(path_template path, router r){
    detail::dynamic_router_tree(request::method::DELETE).insert(path, r);
}

void clear(request::method m){
    detail::dynamic_router_tree(m).clear();
}

} // namespace dynamic


void get(std::string_view path, router r){
    detail::static_router_map(request::method::GET).emplace(path, r);
}

void head(std::string_view path, router r){
    detail::static_router_map(request::method::HEAD).emplace(path, r);
}

void post(std::string_view path, router r){
    detail::static_router_map(request::method::POST).emplace(path, r);
}

void put(std::string_view path, router r){
    detail::static_router_map(request::method::PUT).emplace(path, r);
}

void del(std::string_view path, router r){
    detail::static_router_map(request::method::DELETE).emplace(path, r);
}

}