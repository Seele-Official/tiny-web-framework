#include "basic.h"

namespace seele::basic {

    std::vector<std::string_view> split_string_view(std::string_view str, std::string_view delimiter) {
        std::vector<std::string_view> result;
        size_t pos = 0;
        while (true) {
            size_t next_pos = str.find(delimiter, pos);
            if (next_pos == std::string_view::npos) {
                result.push_back(str.substr(pos));
                break;
            }
            result.push_back(str.substr(pos, next_pos - pos));
            pos = next_pos + delimiter.size();
        }
        return result;
    }
    std::vector<std::string_view> split_string_view(std::string_view str, char delimiter) {
        std::vector<std::string_view> result;
        size_t pos = 0;
        while (true) {
            size_t next_pos = str.find(delimiter, pos);
            if (next_pos == std::string_view::npos) {
                result.push_back(str.substr(pos));
                break;
            }
            result.push_back(str.substr(pos, next_pos - pos));
            pos = next_pos + 1;
        }
        return result;
    }

} // namespace seele::basic