#pragma once
#include <string_view>

struct app{
    app& set_root_path(std::string_view path);

    app& set_addr(std::string_view addr_str);

    void run();
};

app& app();