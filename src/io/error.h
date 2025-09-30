#pragma once
#include <string_view>
#include <cstdint>
#include <cstring>


namespace io::error {
    constexpr int32_t SYS =        -1;
    constexpr int32_t CTX_CLOSED = -2;
    constexpr int32_t TIMEOUT =    -3;
    
    inline thread_local std::string_view msg = "";  
    inline thread_local int32_t code = 0;

namespace detail {
    inline void set_code(int32_t e){
        io::error::code = -e;
        io::error::msg = strerror(-e);
    }

    inline void set_msg(std::string_view e){
        io::error::msg = e;
    }
}

} // namespace io::error