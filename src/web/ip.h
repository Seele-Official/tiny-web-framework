#pragma once
#include <charconv>
#include <cstdint>
#include <cstring>
#include <vector>
#include <format>
#include <ranges>

#ifdef __linux__
#include <netinet/in.h>
#endif



namespace web::ip {

template <std::integral T>
constexpr T ntoh(T value) {
    if constexpr (std::endian::native == std::endian::big) {
        return value; 
    } else {
        return std::byteswap(value);
    }
}
template <std::integral T>
constexpr T hton(T value) {
    if constexpr (std::endian::native == std::endian::big) {
        return value; 
    } else {
        return std::byteswap(value);
    }
}

constexpr std::string inet_ntoa(uint32_t addr) {
    return std::views::iota(0, 4)
        | std::views::transform([&addr](int){
            std::string part = std::to_string(addr & 0xFF);
            addr >>= 8;
            return part;
        })
        | std::views::join_with('.')
        | std::ranges::to<std::string>();
}

struct v4{
    uint32_t net_address;
    uint16_t net_port;
    explicit v4() = default;
    explicit v4(uint32_t net_address, uint16_t net_port) : net_address{net_address}, net_port{net_port} {}
    auto operator<=>(const v4&) const = default;
    bool is_valid() const {
        return net_address != 0 && net_port != 0;
    }

    inline std::string to_string() const {
        return std::format("{}:{}", inet_ntoa(net_address), ntoh(net_port));
    }

    inline static v4 from_string(const std::string& addr) {
        auto pos = addr.find(':');
        if (pos == std::string::npos) return v4{};
        std::string ip_str = addr.substr(0, pos);
        std::string port_str = addr.substr(pos + 1);

        auto ip_parts = std::views::split(ip_str, '.')
            | std::views::transform([](auto&& rng){
                return std::string_view(rng.begin(), rng.end());
            })
            | std::ranges::to<std::vector<std::string_view>>();

        if (ip_parts.size() != 4) return v4{};
        uint32_t ip = 0;
        for (auto part : ip_parts) {
            uint8_t byte = 0;
            auto ret = std::from_chars(part.data(), part.data() + part.size(), byte);
            if (ret.ec == std::errc()) {
                ip = (ip << 8) | byte;
            } else {
                return v4{};
            }
        }
        uint16_t port = 0;
        auto ret = std::from_chars(port_str.data(), port_str.data() + port_str.size(), port);
        if (ret.ec != std::errc()) {
            return v4{};
        }

        return v4{hton(ip), hton(port)};
    }
#ifdef __linux__
    inline sockaddr_in to_sockaddr_in() const {
        return sockaddr_in{
            .sin_family = AF_INET,
            .sin_port = net_port,
            .sin_addr = {.s_addr = net_address},
            .sin_zero = {}
        };
    }
    inline static v4 from_sockaddr_in(const sockaddr_in& addr) {
        return v4{addr.sin_addr.s_addr, addr.sin_port};
    }
#endif

};
}