#include "http/response.h"


namespace http::response {

namespace {
    template<typename key_t, typename value_t, size_t N>
struct consexpr_int_map{
    std::array<value_t, N> map;

    consteval consexpr_int_map(std::initializer_list<std::pair<key_t, value_t>> init_list) {
        for (const auto& item : init_list) {
            map[static_cast<size_t>(item.first)] = item.second;
        }
    }

    auto operator[](key_t key) const -> value_t {
        return map[static_cast<size_t>(key)];
    }
};
};

std::string_view status_code_to_string(status_code code){
    constexpr consexpr_int_map<status_code, std::string_view, 900> 
        phrase_contents = {
            {status_code::ok, "OK"},

            {status_code::bad_request, "Bad Request"},
            {status_code::forbidden, "Forbidden"},
            {status_code::not_found, "Not Found"},
            {status_code::method_not_allowed, "Method Not Allowed"},


            {status_code::internal_server_error, "Internal Server Error"},
            {status_code::not_implemented, "Not Implemented"},
        };
    return phrase_contents[code]; 
}

} // namespace http::response
