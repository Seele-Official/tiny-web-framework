#pragma once
#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "json.h"
namespace Json{
struct location{
    size_t line;
    size_t column;
};

struct token{
    enum T{
        LBRACE,
        RBRACE,
        LBRACKET,
        RBRACKET,
        COLON,
        COMMA,
        TRUE,
        FALSE,
        NULL_TYPE,
        STRING,
        NUMBER,
        END
    };
    token::T type;
    std::string_view lexeme;
    location loc;
};

struct error{
    std::string message;
    location primary_span;
    size_t primary_span_length;
    static error make(std::string&& message, const token& tok){
        return error{
            .message = std::move(message),
            .primary_span = tok.loc,
            .primary_span_length = tok.lexeme.size()
        };
    }

    template<typename out_t>
    auto format_to(out_t&& out, const std::vector<std::string_view>& lines) const {
        if (this->primary_span.line < lines.size()) {
            return std::format_to(
                std::forward<out_t>(out),
                "`{}` at line {}:{} \n    {}\n    {}",
                this->message,
                this->primary_span.line,
                this->primary_span.column,
                lines[this->primary_span.line],
                std::string(this->primary_span.column, ' ') + std::string(this->primary_span_length, '^')
            );
        } else if (this->primary_span.line == lines.size()) {
            return std::format_to(
                std::forward<out_t>(out),
                "`{}` at end of input",
                this->message
            );
        } else {
            return std::format_to(
                std::forward<out_t>(out),
                "`{}` at unknown location",
                this->message
            );
        }
    }

    std::string to_string(const std::vector<std::string_view>& lines) const {
        std::string out;
        this->format_to(std::back_inserter(out), lines);
        return out;
    }
};

class lexer {
public:
    static std::expected<std::vector<token>, error> lex(std::vector<std::string_view> lines);
private:
    static std::expected<void, error> lex_line(std::string_view line, size_t line_index, std::vector<token>& out);
};

class parser{
public:
    class iterator {
    public:
        using base_iterator = std::vector<token>::iterator;
        using reference     = typename std::iterator_traits<base_iterator>::reference;
        iterator() = default;
        iterator(base_iterator current, base_iterator end) : current(current), end(end) {}

        reference operator*() const {
            return *this->current;
        }

        iterator& operator++() {
            if (this->current != this->end) {
                base_iterator next = this->current;
                ++next;
                if (next != this->end) {
                    this->current = next;
                }
            }
            return *this;
        }

        iterator operator++(int) {
            iterator temp = *this;
            ++(*this);
            return temp;
        }
        bool operator==(const iterator& other) const = default;

    private:
        base_iterator current{};
        base_iterator end{};
    };

    parser() = default;

    std::expected<Json::json, error> parse(iterator it);

private:

    std::expected<Json::null, error>    parse_null();
    std::expected<Json::value, error>   parse_value();
    std::expected<Json::array, error>   parse_array();
    std::expected<Json::object, error>  parse_object();
    std::expected<Json::number, error>  parse_number();
    std::expected<Json::string, error>  parse_string();
    std::expected<Json::boolean, error> parse_boolean();

    token& get_current(){
        return *this->current;
    }

    token& consume() {
        return *this->current++;
    }
    iterator current;
};

std::expected<json, std::string> parse(std::string_view input);

std::expected<std::vector<token>, std::string> lex(std::string_view input);
}