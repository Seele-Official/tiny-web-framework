#include "json.h"
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace std {
    size_t hash<Json::string>::operator()(const Json::string& s) const noexcept {
        return std::hash<std::string>()(static_cast<std::string>(s));
    }

    fmt_ctx::iterator formatter<Json::null>::format(const Json::null&, fmt_ctx& ctx) const {
        return format_to(ctx.out(), "null");
    }

    fmt_ctx::iterator formatter<Json::number>::format(const Json::number& n, fmt_ctx& ctx) const {
        return std::visit(
            [&](auto&& arg) {
                return std::format_to(ctx.out(), "{}", arg);
            },
            n
        );
    }

    fmt_ctx::iterator formatter<Json::string>::format(const Json::string& s, fmt_ctx& ctx) const {
        return format_to(ctx.out(), R"("{}")", (std::string)s);
    }

    fmt_ctx::iterator formatter<Json::object>::format(const Json::object& obj, fmt_ctx& ctx) const {
        auto it = format_to(ctx.out(), "{{");
        if (!obj.empty()) {
            const auto& [key, value] = *obj.begin();
            it = format_to(it, "{}: {}", key, value);

            for (const auto& [k, v] : obj | std::views::drop(1)) {
                it = format_to(it, ", {}: {}", k, v);
            }
        }
        return format_to(it, "}}");
    }

    fmt_ctx::iterator formatter<Json::array>::format(const Json::array& arr, fmt_ctx& ctx) const {
        auto it = format_to(ctx.out(), "[");

        if (!arr.empty()) {
            auto f = arr.front();
            it = format_to(it, "{}", f);

            for (auto&& elem : arr | std::views::drop(1)) {
                it = format_to(it, ", {}", elem);
            }
        }
        return format_to(it, "]");
    }

    fmt_ctx::iterator formatter<Json::json>::format(const Json::json& j,  fmt_ctx& ctx) const {
        return std::visit([&](auto&& arg) {
            return format_to(ctx.out(), "{}", arg);
        }, j);
    }
}