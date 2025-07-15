#pragma once
#include <concepts>
#include <expected>
#include <optional>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include "http.h"
#include "io.h"
#include "meta.h"

struct http_file_ctx{
    iovec_wrapper header;
    iovec data;
    static http_file_ctx make(http::res_msg& msg, void* file, size_t size) {
        http_file_ctx res{
            {256},
            {file, size}
        };
        msg.refresh_content_length(size);
        auto it = msg.format_to(static_cast<char*>(res.header.iov_base));
        res.header.iov_len = seele::meta::safe_cast<size_t>(it - static_cast<char*>(res.header.iov_base));
        return res;
    }
    static http_file_ctx make(http::res_msg&& msg, void* file, size_t size) {
        http_file_ctx res{
            {256},
            {file, size}
        };
        msg.refresh_content_length(size);
        auto it = msg.format_to(static_cast<char*>(res.header.iov_base));
        res.header.iov_len = seele::meta::safe_cast<size_t>(it - static_cast<char*>(res.header.iov_base));
        return res;
    }
};

using handler_response = std::variant<http::res_msg, http_file_ctx>;
struct app{
    app& set_root_path(std::string_view path);

    app& set_addr(std::string_view addr_str);

    template<typename invocable_t>
        requires std::is_invocable_r_v<std::expected<handler_response, http::error_code>, invocable_t, const http::query_t&, const http::header_t&>
    app& GET(std::string_view path, invocable_t& handler);

    app& GET(std::string_view path, void* helper_ptr, auto (*handler)(void*, const http::query_t&, const http::header_t&) -> std::expected<handler_response, http::error_code>);

    void run();
};

template<typename invocable_t>
    requires std::is_invocable_r_v<std::expected<handler_response, http::error_code>, invocable_t, const http::query_t&, const http::header_t&>
app& app::GET(std::string_view path, invocable_t& handler){
    using type = std::decay_t<invocable_t>;
    if constexpr (std::is_same_v<type, auto (*)(const http::query_t&, const http::header_t&) -> std::expected<handler_response, http::error_code>>) {
        return GET(path, nullptr, handler);
    } else {
        return GET(path, &handler, [](void* helper_ptr, const http::query_t& query, const http::header_t& header) -> std::expected<handler_response, http::error_code> {
            constexpr auto (type::*func)(const http::query_t&, const http::header_t&) -> std::expected<handler_response, http::error_code> = &type::operator();

            return (static_cast<invocable_t*>(helper_ptr)->*func)(query, header);
        });
    }

}



app& app();