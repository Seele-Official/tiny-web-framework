#pragma once
#include <generator>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <variant>
#include <list>
#include <expected>
#include <vector>



namespace seele::opts{
struct ruler{
    enum class type_t {
        no_argument,
        required_argument,
        optional_argument
    };
    std::string_view short_name;
    std::string_view long_name;
    const type_t type;

    consteval ruler(std::string_view long_name, type_t t, std::string_view short_name = "") : short_name(short_name), long_name(long_name), type(t) {
        #define throw_if(condition, message) \
            if (condition && (std::terminate(), message)){}
                // throw message; // if exceptions were allowed
        if (!short_name.empty()){
            throw_if(short_name.size() != 2, "Short option must be exactly 2 characters");
            throw_if(!short_name.starts_with("-"), "Short option must start with '-'");
        }
        throw_if(long_name.size() <= 3, "Long option must be at least 4 characters");
        throw_if(!long_name.starts_with("--"), "Long option must start with '--'");
    }

    static consteval ruler no_arg(std::string_view ln, std::string_view sn = "") {
        return ruler(ln, type_t::no_argument, sn);
    }

    static consteval ruler req_arg(std::string_view ln, std::string_view sn = "") {
        return ruler(ln, type_t::required_argument, sn);
    }

    static consteval ruler opt_arg(std::string_view ln, std::string_view sn = "") {
        return ruler(ln, type_t::optional_argument, sn);
    }
};

struct no_arg {
    std::string_view short_name;
    std::string_view long_name;
};

struct req_arg {
    std::string_view short_name;
    std::string_view long_name;
    std::string_view value;
};


struct opt_arg {
    std::string_view short_name;
    std::string_view long_name;
    std::optional<std::string_view> value;
};

struct pos_arg {
    std::list<std::string_view> values;
};  

template <size_t N>
class opts_impl;

template <typename... args_t>
    requires (std::is_same_v<ruler, std::decay_t<args_t>> && ...)
opts_impl(args_t&&...) -> opts_impl<sizeof...(args_t)>;


using item = std::variant<
    no_arg,
    req_arg,
    opt_arg,
    pos_arg
>;
using parse_result = std::expected<item, std::string>;

std::generator<parse_result> parse(std::span<const ruler> rs, int argc, char** argv);

template <size_t N>
class opts_impl{
public:

    const ruler rs[N];
    template <typename... args_t>
        requires (std::is_same_v<ruler, std::decay_t<args_t>> && ...)
    consteval opts_impl(args_t&&... args) : rs{ std::forward<args_t>(args)... } {}


    std::generator<parse_result> parse(int argc, char** argv) {
        return seele::opts::parse({rs, N}, argc, argv);
    }
};


template <typename... args_t>
    requires (std::is_same_v<ruler, std::decay_t<args_t>> && ...)
consteval auto make_opts(args_t&&... args) {
    return opts_impl{std::forward<args_t>(args)...};
}

};

