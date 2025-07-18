#pragma once
#include <string_view>
#include <vector>
#include <array>
#include <cstdint>
namespace seele::basic {
    constexpr std::array<bool, 256> is_hex_digit_helper_map(){
        std::array<bool, 256> hex_digit_map{};
        for (char c : "0123456789ABCDEFabcdef") {
            hex_digit_map[static_cast<unsigned char>(c)] = true;
        }
        return hex_digit_map;
    }

    inline constexpr bool is_hex_digit(char c) {
        constexpr std::array<bool, 256> hex_digit_map = is_hex_digit_helper_map();
        return hex_digit_map[static_cast<unsigned char>(c)];
    }

    constexpr std::array<uint8_t, 256> hex_to_int_helper_map(){
        std::array<uint8_t, 256> map{};
        map.fill(0xFF);
        map['0'] = 0x00; map['1'] = 0x01; map['2'] = 0x02; map['3'] = 0x03;
        map['4'] = 0x04; map['5'] = 0x05; map['6'] = 0x06; map['7'] = 0x07;
        map['8'] = 0x08; map['9'] = 0x09; map['A'] = 0x0A; map['B'] = 0x0B;
        map['C'] = 0x0C; map['D'] = 0x0D; map['E'] = 0x0E; map['F'] = 0x0F;
        map['a'] = 0x0A; map['b'] = 0x0B; map['c'] = 0x0C; map['d'] = 0x0D;
        map['e'] = 0x0E; map['f'] = 0x0F;
        return map;
    }

    inline constexpr uint8_t hex_to_int(char c){
        constexpr std::array<uint8_t, 256> hex_to_int_map = hex_to_int_helper_map();
        return hex_to_int_map[static_cast<unsigned char>(c)];
    }


    std::vector<std::string_view> split_string_view(std::string_view str, std::string_view delimiter);
    std::vector<std::string_view> split_string_view(std::string_view str, char delimiter);
}