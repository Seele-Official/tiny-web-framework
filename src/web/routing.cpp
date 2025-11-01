#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <sys/stat.h>
#include <unordered_map>
#include <utility>
#include <vector>

#include "logging/log.h"
#include "web/routing.h"
#include "web/mime.h"
#include "http/http.h"




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
        {
            "*", 
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
        auto& map = static_router_map(req.line.method);
        auto& tree = dynamic_router_tree(req.line.method);

        std::error_code ec;
        auto pct_decoded_path = http::pct_decode(
            origin->path, ec
        );
        
        if (ec) {
            logging::async::error("Failed to decode URI `{}`: {}", origin->path, ec.message());
            return web::response::error(http::response::status_code::bad_request);
        }

        if (auto it = map.find(pct_decoded_path); it != map.end()) {
            return it->second(req);
        } else if (auto ret = 
                tree.route(
                    std::string_view{pct_decoded_path}
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

namespace env {

struct resource_router{
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

struct resource_head_router{
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

std::vector<std::pair<resource_head_router, resource_router>>& static_routers(){
    static std::vector<std::pair<resource_head_router, resource_router>> static_routers{};
    return static_routers;

} 

}

void add_static_resource_router(const std::filesystem::path& file_path, const std::string& route_path) {
    std::error_code ec;

    auto file_size = std::filesystem::file_size(file_path, ec);
    if (ec) {
        std::println(stderr, "Failed to get file size for {}: {}", file_path.string(), ec.message());
        std::terminate();
    }
    if (file_size == 0) {
        // Silently skip empty files, or log if desired
        logging::sync::warn("Skipping empty file: {}", file_path.string());
        return;
    }

    auto fd = io::fd::open_file(file_path, O_RDONLY);
    if (!fd.is_valid()) {
        std::println(stderr, "Failed to open file: {}", file_path.string());
        std::terminate();
    }

    std::string content_type = "application/octet-stream"; // Default
    std::string ext = file_path.extension().string();
    if (auto it = web::mime_types.find(ext); it != web::mime_types.end()) {
        content_type = it->second;
    }

    routing::env::static_routers().push_back({
        routing::env::resource_head_router{
            route_path,
            content_type,
            (size_t) file_size
        },
        routing::env::resource_router{
            route_path,
            content_type,
            {(size_t) file_size, PROT_READ, MAP_SHARED, fd.get(), 0}
        }
    });

    auto& [head_router, get_router] = routing::env::static_routers().back();
    routing::get(get_router.name, get_router);
    routing::head(head_router.name, head_router);

    logging::sync::info("Adding static resource route: `{}` -> `{}`", route_path, file_path.string());
}

void configure_static_resource_routes() {
    auto root = std::filesystem::absolute(routing::env::root_path());

    if (!std::filesystem::exists(root) || !std::filesystem::is_directory(root)) {
        std::println(stderr, "Root path is not a valid directory: {}", root.string());
        std::terminate();
    }

    routing::env::static_routers().reserve(512);

    for (const auto& index_filename : routing::env::index_files()) {
        auto index_path = root / index_filename;

        if (std::filesystem::exists(index_path) && std::filesystem::is_regular_file(index_path)) {
            auto full_path = std::filesystem::absolute(index_path);

            // The route for an index file in the root directory is "/"
            add_static_resource_router(full_path, "/");

            break; // Found an index file for the root, stop searching
        }
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file()) {
            auto full_path = std::filesystem::absolute(entry.path());
            
            // Use generic_string() to ensure forward slashes in the route
            auto relative_path = full_path.lexically_relative(root).generic_string();
            auto route_str = std::format("/{}", relative_path);

            add_static_resource_router(full_path, route_str);

        } else if (entry.is_directory()) {
            // Check for default index files in this directory
            for (const auto& index_filename : routing::env::index_files()) {
                auto index_path = entry.path() / index_filename;

                if (std::filesystem::exists(index_path) && std::filesystem::is_regular_file(index_path)) {
                    auto full_path = std::filesystem::absolute(index_path);

                    // The route for an index file is its parent directory's path
                    auto relative_dir_path = full_path.parent_path().lexically_relative(root).generic_string();
                    
                    
                    add_static_resource_router(full_path, std::format("/{}/", relative_dir_path));
                    add_static_resource_router(full_path, std::format("/{}", relative_dir_path));
                    // Found an index file for this directory, stop searching
                    break; 
                }
            }
        }
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