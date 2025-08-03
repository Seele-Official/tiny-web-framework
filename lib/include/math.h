#pragma once
#include <concepts>
#include <random>
#include <cstdint>
#include <array>
#include <expected>
#include <format>
namespace seele::math {
template <std::integral T>
T random(T min, T max) {
    static std::random_device rd; 
    std::uniform_int_distribution<T> dist(min, max - 1); 
    return dist(rd);
}

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


uint32_t crc32_bitwise(const uint8_t* data, size_t len);

std::expected<uint32_t, char> stoi(std::string_view str);

std::string tohex(void* ptr, size_t size);

template <typename T>
constexpr auto tohex(T struct_t){
    return math::tohex(&struct_t, sizeof(T));
}
}

