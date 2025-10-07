#pragma once
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <utility>
#include <variant>
#include <string>
#include <unordered_map>
#include <vector>

namespace Json{

struct string: std::string {
    using std::string::string;
    template<typename... args_t>
        requires std::constructible_from<std::string, args_t&&...>
    string(args_t&&... args) : std::string(std::forward<args_t>(args)...) {}
};

}

namespace std {

template <>
struct hash<Json::string> {
    size_t operator()(const Json::string& s) const noexcept;
};

}


namespace Json {

struct json;

struct null {
    bool operator==(null) const { return true; }
};

struct number : std::variant<int64_t>{
    using std::variant<int64_t>::variant;
};

using boolean = bool;
using object = std::unordered_map<string, json>;
using array = std::vector<json>;
using value = std::variant<null, number, string, boolean, object, array>;


struct json: value{
    using value::value;
    template<typename... args_t>
        requires std::constructible_from<value, args_t&&...>
    json(args_t&&... args) : value(std::forward<args_t>(args)...) {}

    std::optional<std::reference_wrapper<json>> at(size_t index) {
        if (auto* obj = std::get_if<array>(this)) {
            if (index < obj->size()) {
                return std::ref((*obj)[index]);
            }
        }
        return std::nullopt;
    }

    std::optional<std::reference_wrapper<json>> at(const string& key) {
        if (auto* obj = std::get_if<object>(this)) {
            if (auto it = obj->find(key); it != obj->end()) {
                return std::ref(it->second);
            }
        }
        return std::nullopt;
    }


    json& operator[](size_t index) {
        if (auto* obj = std::get_if<array>(this)) {
            if (index < obj->size()) {
                return (*obj)[index];
            }
        }
        std::terminate();
    }

    json& operator[](const string& key) {
        if (auto* obj = std::get_if<object>(this)) {
            if (auto it = obj->find(key); it != obj->end()) {
                return it->second;
            }
        }
        std::terminate();
    }

    template<typename T>
    std::optional<std::reference_wrapper<T>> as() {
        if (auto* v = std::get_if<T>(this)) {
            return std::ref(*v);
        }
        return std::nullopt;
    }

};
}

namespace std {

using fmt_ctx = format_context;
using fmt_pctx = format_parse_context;

template <>
struct formatter<Json::null> {
    constexpr fmt_pctx::iterator parse(fmt_pctx& ctx) { return ctx.begin(); }
    fmt_ctx::iterator format(const Json::null&, fmt_ctx& ctx) const;
};

template <>
struct formatter<Json::number> {
    constexpr fmt_pctx::iterator parse(fmt_pctx& ctx) { return ctx.begin(); }
    fmt_ctx::iterator format(const Json::number& n, fmt_ctx& ctx) const;
};

template <>
struct formatter<Json::string> {
    constexpr fmt_pctx::iterator parse(fmt_pctx& ctx) { return ctx.begin(); }
    fmt_ctx::iterator format(const Json::string& s, fmt_ctx& ctx) const;
};

template <>
struct formatter<Json::json> {
    constexpr fmt_pctx::iterator parse(fmt_pctx& ctx) { return ctx.begin(); }
    fmt_ctx::iterator format(const Json::json& j, fmt_ctx& ctx) const;
};

template <>
struct formatter<Json::object> {
    constexpr fmt_pctx::iterator parse(fmt_pctx& ctx) { return ctx.begin(); }
    fmt_ctx::iterator format(const Json::object& obj, fmt_ctx& ctx) const;
};


template <>
struct formatter<Json::array> {
    constexpr fmt_pctx::iterator parse(fmt_pctx& ctx) { return ctx.begin(); }
    fmt_ctx::iterator format(const Json::array& arr, fmt_ctx& ctx) const;
};
}



#include "parser.h"