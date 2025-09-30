#pragma once
#include <cstdint>
#include <cstring>
#include <compare>
#include <format>

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
    addr = ntoh(addr);
    std::string ip;
    for (size_t i = 0; i < 4; i++) {
        ip.insert(0, std::to_string(addr & 0xFF));
        if (i != 3) ip.insert(0, ".");
        addr >>= 8;
    }
    return ip;
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