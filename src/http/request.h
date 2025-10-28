#pragma once
#include <optional>
#include <queue>
#include <string_view>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include "coro/sendable_task.h"
#include "meta.h"


namespace http::request {

enum class method {
    GET,
    POST,
    PUT,
    DELETE,
    HEAD,
    OPTIONS,
    CONNECT,
    TRACE
};

using header = std::unordered_map<std::string, std::string>;
using path = std::string;
using query = std::string;
using body = std::string;

struct origin_form{
    request::path  path;
    request::query query;
};

struct absolute_form{};
struct authority_form{};
struct asterisk_form{};

using target = std::variant<
    origin_form, absolute_form, authority_form, asterisk_form
>;

struct line{
    template<typename out_t>
    auto format_to(out_t&& out) const {
        return std::format_to(
            std::forward<out_t>(out), 
            "{} {} {}\r\n", 
            meta::enum_to_string(method),
            "this_is_a_placeholder", // TODO: Implement target
            version
        );
    }
    request::method method;
    request::target target;
    std::string     version;
};

struct msg{    
    void clear() {
        line = {};
        header.clear();
        body.clear();
    }
    template<typename out_t>
    auto format_to(out_t&& out) const {
        auto it = line.format_to(std::forward<out_t>(out));
        for (const auto& [key, value] : header) {
            it = std::format_to(it, "{}: {}\r\n", key, value);
        }
        it = std::format_to(it, "\r\n{}", body);
        return it;
    }
    request::line   line{};
    request::header header{};
    request::body   body{};
};

class parser {
public:
    using parse_task = coro::sendable_task<void, std::string_view>;
    parser() = default;
    parser(const parser&) = delete;
    parser(parser&&) = delete;
    parser& operator=(const parser&) = delete;
    parser& operator=(parser&&) = delete;
    ~parser() = default;

    void feed(std::string_view data) {
        this->t.send_and_resume(data);
    }

    bool empty() const {
        return this->msg_queue.empty();
    }

    std::optional<msg> pop_front(){
        auto ret = std::move(this->msg_queue.front());
        this->msg_queue.pop();
        return ret;
    };
    

private:
    void fail_parse() {
        this->msg_queue.push(std::nullopt);
        this->line_buffer.clear();
        this->data_view = std::string_view{};
    }
    std::optional<std::string_view> get_line();


    static parse_task task(parser*);

    std::string                    line_buffer{};
    std::string_view               data_view{};
    parse_task                     t{this->task(this)};
    std::queue<std::optional<msg>> msg_queue{};
};

} // namespace http::request
