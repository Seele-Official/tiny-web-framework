#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <format>
#include <expected>
#include "math.h"

#ifdef __linux__
#include <netinet/in.h>
#endif


namespace seele::net{

std::expected<uint32_t, std::string> inet_addr(std::string_view ip);

std::string inet_ntoa(uint32_t addr);



struct ipv4{
    uint32_t net_address;
    uint16_t net_port;
    explicit ipv4() : net_address{}, net_port{} {}
    explicit ipv4(uint32_t net_address, uint16_t net_port) : net_address{net_address}, net_port{net_port} {}
    auto operator<=>(const ipv4&) const = default;
    bool is_valid() const {
        return net_address != 0 && net_port != 0;
    }

    inline std::string toString() const {
        return std::format("{}:{}", inet_ntoa(net_address), math::ntoh(net_port));
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
    inline static ipv4 from_sockaddr_in(const sockaddr_in& addr) {
        return ipv4{addr.sin_addr.s_addr, addr.sin_port};
    }
#endif

};

std::expected<ipv4, std::string> parse_addr(std::string_view addr);

}