#include <ranges>

#include "meta.h"
#include "http/request.h"

namespace http::request {

namespace {
constexpr char CR = '\r';
constexpr char LF = '\n';
constexpr char SP = ' ';
constexpr char HTAB = '\t';

constexpr std::string_view CRLF = "\r\n";

constexpr bool is_tchar(char c) {
    constexpr auto map = 
        [](){
            std::array<bool, 256> map{};
            for (auto& c :"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz!#$%&'*+-.^_`|~"){
                map[static_cast<unsigned char>(c)] = true;
            }
            return map;
        }();

    return map[static_cast<unsigned char>(c)];
}
constexpr bool is_pchar_with_out_pct(char c) {
    constexpr auto map = 
        [](){
            std::array<bool, 256> map{};
            for (auto& c :"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-._~!$&'()*+,;=:@"){
                map[static_cast<unsigned char>(c)] = true;
            }
            return map;
        }();

    return map[static_cast<unsigned char>(c)];
}




template <std::forward_iterator It, std::sentinel_for<It> Sent>
    requires std::same_as<std::iter_value_t<It>, char>
bool is_query(It first, Sent last) {
    while (first != last) {
        if (is_pchar_with_out_pct(*first) || *first == '?' || *first == '/') {
            ++first;
        } else if(*first == '%') {
            ++first;
            if (first == last) return false;
            if (!std::isxdigit(static_cast<unsigned char>(*first))) return false;

            ++first;
            if (first == last) return false; 
            if (!std::isxdigit(static_cast<unsigned char>(*first))) return false;

            ++first; 
        } else {
            return false;
        }
    }
    return true;
}

template <std::ranges::forward_range R>
    requires std::same_as<std::ranges::range_value_t<R>, char>
bool is_query(R&& range) {
    return is_query(std::ranges::begin(range), std::ranges::end(range));
}


template <std::forward_iterator It, std::sentinel_for<It> Sent>
    requires std::same_as<std::iter_value_t<It>, char>
bool is_absolut_path(It first, Sent last) {
    while (first != last) {
        if (is_pchar_with_out_pct(*first) || *first == '/') {
            ++first;
        } else if(*first == '%') {
            ++first;
            if (first == last) return false;
            if (!std::isxdigit(static_cast<unsigned char>(*first))) return false;

            ++first;
            if (first == last) return false; 
            if (!std::isxdigit(static_cast<unsigned char>(*first))) return false;

            ++first; 
        } else {
            return false;
        }
    }
    return true;
}

template <std::ranges::forward_range R>
    requires std::same_as<std::ranges::range_value_t<R>, char>
bool is_absolut_path(R&& range) {
    return is_absolut_path(std::ranges::begin(range), std::ranges::end(range));
}



constexpr std::string_view trim(std::string_view in) {
    auto is_space = [](char c){ 
        return c == SP || c == HTAB; 
    };

    auto view = in
        | std::views::drop_while(is_space)
        | std::views::reverse
        | std::views::drop_while(is_space)
        | std::views::reverse;

    return {view.begin().base().base(), view.end().base().base()};
}

};

template <typename lambda_t>
    requires std::is_same_v<bool, std::invoke_result_t<lambda_t, char>>
std::string_view parse_token(std::string_view str, lambda_t&& is_valid) {
    for (auto [i, c] : std::views::enumerate(str)){
        if (!is_valid(c)) {
            return str.substr(0, i);
        }
    }
    return str;
}

std::optional<target> parse_target(std::string_view str){
    if (str.starts_with("/")){
        // origin form
        std::string_view path{};
        std::string_view query{};
        auto pos = str.find('?');

        if (pos == std::string_view::npos){
            path = str;
        } else {
            path = str.substr(0, pos);
            query = str.substr(pos + 1);
        }

        // check path and query validity
        if (!is_absolut_path(path) || !is_query(query)){
            return std::nullopt;
        }

        return origin_form{std::string(path), std::string(query)};
    }

    else if (str == "*") {
        return asterisk_form{};
    }
    //TODO: implement absolute form and authority form

    return absolute_form{};
}

std::optional<std::string_view> parser::get_line() {
    if (auto line_end = this->data_view.find(CRLF); line_end != std::string_view::npos){
        auto line_view = this->data_view.substr(0, line_end);
        this->data_view.remove_prefix(line_end + CRLF.size());
        if (!this->line_buffer.empty()){
            this->line_buffer.append(line_view);
            line_view = this->line_buffer;
            this->line_buffer.clear();
        }
        return line_view;
    } else {
        this->line_buffer.append(this->data_view);
        this->data_view = std::string_view{};
        return std::nullopt;
    }
}

parser::parse_task parser::task(parser* p) {
    using wait_message = parser::parse_task::wait_message;

    auto instance = p;

    instance->data_view = co_await wait_message{};

    while (true) {
        start_over:
        
        request::msg request{};
    
        // Parse request line
        std::optional<std::string_view> line_opt{};

        while (!(line_opt = instance->get_line())) {
            instance->data_view = co_await wait_message{};
        }
        auto line_parts = line_opt.value()
            | std::views::split(SP)
            | std::views::transform([](auto&& rng) {
                return std::string_view(rng);
            })
            | std::ranges::to<std::vector<std::string_view>>();

        if (line_parts.size() != 3) {
            instance->fail_parse();
            goto start_over;
        }

        auto method_opt = 
            meta::enum_from_string<method>(line_parts[0]);
        if (!method_opt) {
            instance->fail_parse();
            goto start_over;
        }

        auto target_opt = 
            parse_target(line_parts[1]);
        if (!target_opt) {
            instance->fail_parse();
            goto start_over;
        }

        request.line = {
            *method_opt,
            std::move(target_opt.value()),
            std::string(line_parts[2])
        };



        // Parse headers
        while (!instance->data_view.starts_with(CRLF)) {
            std::optional<std::string_view> line_opt;
            while (!(line_opt = instance->get_line())) {
                instance->data_view = co_await wait_message{};
            }
            auto line_view = line_opt.value();

            auto key = parse_token(line_view, is_tchar);
            line_view.remove_prefix(key.size());
            if (key.empty()) {
                instance->fail_parse();
                goto start_over;
            }
            if (line_view.empty() || line_view.front() != ':') {
                instance->fail_parse();
                goto start_over;
            }
            line_view.remove_prefix(1); // Skip ':'

            request.header.emplace(trim(key), trim(line_view));


            if (instance->data_view.empty()) {
                instance->data_view = co_await wait_message{};
            }
        }
        instance->data_view.remove_prefix(CRLF.size());

        // Parse body if Content-Length is present
        if(auto content_length_it = request.header.find("Content-Length"); content_length_it != request.header.end()) {
            size_t content_length{};

            auto str = content_length_it->second;

            auto [ptr, ec] = std::from_chars(
                str.data(), 
                str.data() + str.size(),
                content_length
            );

            if (ec != std::errc{}) {
                instance->fail_parse();
                goto start_over;  // Invalid Content-Length
            }

            while (request.body.size() < content_length) {
                auto need_to_read = content_length - request.body.size();
                if (need_to_read > instance->data_view.size()) {
                    request.body.append(instance->data_view);
                    instance->data_view = co_await wait_message{};
                } else {
                    request.body.append(instance->data_view.substr(0, need_to_read));
                    instance->data_view.remove_prefix(need_to_read);
                }
            }
        }

        instance->msg_queue.push(std::move(request));
    }
}




} // namespace http::request
