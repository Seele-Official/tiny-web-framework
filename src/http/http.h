#pragma once
#include <charconv>
#include <optional>
#include <string>
#include <system_error>

namespace http {
template <std::forward_iterator It, std::sentinel_for<It> Sent>
    requires std::same_as<std::iter_value_t<It>, char>
std::string pct_decode(It first, Sent last, std::error_code& ec) {
    std::string result;

    if constexpr (std::sized_sentinel_for<Sent, It>) {
        result.reserve(std::distance(first, last));
    }

    while (first != last) {
        if (*first == '%') {
            ++first;
            if (first == last) {
                ec = std::make_error_code(std::errc::invalid_argument);
                return {};
            }
            
            char hex_chars[2];
            hex_chars[0] = *first;

            ++first;
            if (first == last) {
                ec = std::make_error_code(std::errc::invalid_argument);
                return {};
            }
            hex_chars[1] = *first;

            char decoded_val;

            auto res = std::from_chars(hex_chars, hex_chars + 2, decoded_val, 16);

            if (res.ec != std::errc{}) {
                ec = std::make_error_code(res.ec);
                return {};
            }

            result.push_back(static_cast<char>(decoded_val));
            ++first;

        } else {
            result.push_back(*first);
            ++first;
        }
    }

    return result;
}

template <std::ranges::forward_range R>
    requires std::same_as<std::ranges::range_value_t<R>, char>
auto pct_decode(R&& range, std::error_code& ec) {
    return pct_decode(std::ranges::begin(range), std::ranges::end(range), ec);
}
} // namespace http

#include "http/request.h"
#include "http/response.h"