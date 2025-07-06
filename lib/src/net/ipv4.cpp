#include "net/ipv4.h"

namespace seele::net{

    std::expected<uint32_t, std::string> inet_addr(std::string_view ip) {
        uint32_t addr = 0;
        for (size_t i = 0; i < 3; i++) {
            auto pos = ip.find('.');
            if (pos == std::string_view::npos) {
                return std::unexpected{"missing '.'"};
            }
            auto num = math::stoi(ip.substr(0, pos));
            if (!num.has_value()) {
                return std::unexpected{std::format("unexpected char: {}", math::tohex(num.error()))};
            }
            addr = (addr << 8) | num.value();
            ip.remove_prefix(pos + 1);
        }
        auto num = math::stoi(ip);
        if (!num.has_value()) {
            return std::unexpected{std::format("unexpected char: {}", math::tohex(num.error()))};
        }
        addr = (addr << 8) | num.value();

        return math::hton(addr);
    }

    std::string inet_ntoa(uint32_t addr) {
        addr = math::ntoh(addr);
        std::string ip;
        for (size_t i = 0; i < 4; i++) {
            ip.insert(0, std::to_string(addr & 0xFF));
            if (i != 3) ip.insert(0, ".");
            addr >>= 8;
        }
        return ip;
    }

    std::expected<ipv4, std::string> parse_addr(std::string_view addr){

        auto pos = addr.find(':');
        if (pos == std::string_view::npos){
            return std::unexpected("parse addr error: missing ':'");
        }

        auto ip = addr.substr(0, pos);
        auto port = addr.substr(pos + 1);

        auto ipaddr = inet_addr(ip);
        if (!ipaddr.has_value()){
            return std::unexpected(std::format("parse ip error: {}", ipaddr.error()));
        }
        auto portnum = math::stoi(port);
        if (!portnum.has_value()){
            return std::unexpected(std::format("parse port error: unexpected char '{}'", math::tohex(portnum.error())));
        }
        return ipv4{ipaddr.value(), math::hton<uint16_t>(portnum.value())};
    }
}