#include "opts.h"

namespace seele::opts {


std::generator<parse_result> parse(std::span<const ruler> rs, int argc, char** argv) {
    std::list<std::string_view> args{argv + 1, argv + argc};

    for (const ruler& r : rs) {
        switch (r.type) {
            case ruler::type_t::no_argument:{
                auto it = std::ranges::find_if(args, [&](const std::string_view& arg) {
                    return arg == r.short_name || arg == r.long_name;
                });
                if (it != args.end()) {
                    args.erase(it);
                    co_yield no_arg{r.short_name, r.long_name};
                }
            }
            break;
            case ruler::type_t::required_argument:{
                auto it = std::ranges::find_if(args, [&](const std::string_view& arg) {
                    return arg == r.short_name || arg == r.long_name;
                });
                if (it != args.end()){
                    if (std::next(it) != args.end() && !(*std::next(it)).starts_with('-')) {
                        std::string_view arg_value = *std::next(it);
                        args.erase(it, std::next(it, 2));
                        co_yield req_arg{r.short_name, r.long_name, arg_value};
                    } else {
                        co_yield std::unexpected{"Required argument missing for " + std::string(r.short_name)};
                    }
                }
                    
            }
            break;
            case ruler::type_t::optional_argument:{
                auto it = std::ranges::find_if(args, [&](const std::string_view& arg) {
                    return arg == r.short_name || arg == r.long_name;
                });
                if (it != args.end()) {
                    std::optional<std::string_view> arg_value;
                    if (std::next(it) != args.end() && !(*std::next(it)).starts_with('-')) {
                        arg_value = *std::next(it);
                        args.erase(it, std::next(it, 2));
                    } else {
                        args.erase(it);
                    }
                    co_yield opt_arg{r.short_name, r.long_name, arg_value};
                }
            }
            break;
        }
    }

    co_yield pos_arg{std::move(args)};
}

}