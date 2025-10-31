
#include "parser.h"
#include "json.h"

#include <cctype>
#include <charconv>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>
#include <ranges>
namespace Json {

namespace {

std::vector<std::string_view> split_lines(std::string_view input) {
    return input
        | std::views::split('\n')
        | std::views::transform([](auto rng) {
            return std::string_view(rng);
        })
        | std::ranges::to<std::vector>();
}

}

std::expected<std::vector<token>, error> lexer::lex(std::vector<std::string_view> lines){
    std::vector<token> tokens;
    for (auto [i, line]: lines | std::views::enumerate) {
        if (auto res = lex_line(line, i, tokens); !res) {
            return std::unexpected(res.error());
        }
    }
    tokens.push_back(token{token::END, "", {lines.size(), 0}});
    return tokens;
}

std::expected<void, error> lexer::lex_line(std::string_view line_input, size_t line_index, std::vector<token>& out){

    for (size_t column = 0; column < line_input.size();) {

        switch (line_input[column]) {
            case ' ':
            case '\t':
            case '\r':
                break;
            case '{':
                out.push_back(token{token::LBRACE, line_input.substr(column, 1), {line_index, column}});
                break;
            case '}':
                out.push_back(token{token::RBRACE, line_input.substr(column, 1), {line_index, column}});
                break;
            case '[':
                out.push_back(token{token::LBRACKET, line_input.substr(column, 1), {line_index, column}});
                break;
            case ']':
                out.push_back(token{token::RBRACKET, line_input.substr(column, 1), {line_index, column}});
                break;
            case ':':
                out.push_back(token{token::COLON, line_input.substr(column, 1), {line_index, column}});
                break;
            case ',':
                out.push_back(token{token::COMMA, line_input.substr(column, 1), {line_index, column}});
                break;
            case 't':{
                if (line_input.substr(column, 4) == "true") {
                    out.push_back(token{token::TRUE, line_input.substr(column, 4), {line_index, column}});
                    column += 4;
                }
                continue;
            }
            case 'f':{
                if (line_input.substr(column, 5) == "false") {
                    out.push_back(token{token::FALSE, line_input.substr(column, 5), {line_index, column}});
                    column += 5;
                }
                continue;
            }
            case 'n':{
                if (line_input.substr(column, 4) == "null") {
                    out.push_back(token{token::NULL_TYPE, line_input.substr(column, 4), {line_index, column}});
                    column += 4;
                }
                continue;
            }

            case '"': {
                size_t start = column;
                location loc = {line_index, column};
                column++;
                while (true) {
                    if (column == line_input.size()) {
                        return std::unexpected(error{
                            "Unterminated string literal", 
                            location{line_index, column}, 
                            column - start
                        });
                    }

                    if (line_input[column] == '\\') {
                        column++;
                    } else if (line_input[column] == '"') {
                        break;
                    }
                    column++;
                }

                out.push_back(token{token::STRING, line_input.substr(start, column - start + 1), loc});
                break;
            }
            // TODO: add support for floating point numbers
            case '-':
            case '0': case '1': case '2': case '3': case '4': 
            case '5': case '6': case '7': case '8': case '9': {
                size_t start = column;
                location loc = {line_index, column};
                column++;
                while (column < line_input.size() && std::isdigit(line_input[column])) {
                    column++;
                }
                out.push_back(token{token::NUMBER, line_input.substr(start, column - start), loc});
                continue;
            }
            default:
                return std::unexpected(error{
                    std::format("Unexpected character '{}'", line_input[column]),
                    location{line_index, column},
                    1
                });
        }
        column++;
    }

    return {};
}

std::expected<Json::json, error> parser::parse(iterator it){
    this->current = it;
    return this->parse_value();
}


std::expected<Json::null, error> parser::parse_null() {
    if (this->get_current().type == token::NULL_TYPE) {
        this->consume();
        return Json::null{};
    }
    return std::unexpected{
        error::make("Expected null", this->get_current())
    };
}

std::expected<Json::value, error> parser::parse_value() {
    switch (this->get_current().type) {
        case token::NULL_TYPE:
            return parse_null();
        case token::NUMBER:
            return parse_number();
        case token::STRING:
            return parse_string();
        case token::TRUE:
        case token::FALSE:
            return parse_boolean();
        case token::LBRACE:
            return parse_object();
        case token::LBRACKET:
            return parse_array();
        default:
            return std::unexpected{
                error::make("Unexpected token", this->get_current())
            };
    }
}
std::expected<Json::array, error> parser::parse_array() {
    if (this->get_current().type != token::LBRACKET) {
        return std::unexpected(error::make("Expected '['", this->get_current()));
    }
    this->consume();

    Json::array arr;
    arr.reserve(4);
    token* trailing_comma = nullptr;
    while (this->get_current().type != token::RBRACKET) {
        if (auto value = this->parse_value()) {
            arr.push_back(std::move(*value));
        } else {
            return std::unexpected(value.error());
        }
        trailing_comma = nullptr;
        if (this->get_current().type == token::COMMA) {
            trailing_comma = &this->consume();
        } else if (this->get_current().type != token::RBRACKET) {
            return std::unexpected(error::make("Expected ',' or ']'", this->get_current()));
        }
    }
    if (trailing_comma && trailing_comma->type == token::COMMA) {
        return std::unexpected(error::make("Trailing comma in array", *trailing_comma));
    }
    this->consume();
    return arr;
}

std::expected<Json::object, error> parser::parse_object() {
    if (this->get_current().type != token::LBRACE) {
        return std::unexpected(error::make("Expected '{'", this->get_current()));
    }
    this->consume();

    object obj;
    token* trailing_comma = nullptr;
    while (this->get_current().type != token::RBRACE) {

        auto key = this->parse_string();
        if (!key) {
            return std::unexpected(key.error());
        }

        if (this->get_current().type != token::COLON) {
            return std::unexpected(error::make("Expected ':'", this->get_current()));
        }
        this->consume();

        auto value = this->parse_value();

        if (!value) {
            return std::unexpected(value.error());
        }

        obj.emplace(std::move(*key), std::move(*value));

        trailing_comma = nullptr;

        if (this->get_current().type == token::COMMA) {
            trailing_comma = &this->consume();
        } else if (this->get_current().type != token::RBRACE) {
            return std::unexpected(error::make("Expected ',' or '}'", this->get_current()));
        }
    }
    if (trailing_comma && trailing_comma->type == token::COMMA) {
        return std::unexpected(error::make("Trailing comma in object", *trailing_comma));
    }
    this->consume();
    return obj;
}


std::expected<Json::number, error> parser::parse_number() {
    if (this->get_current().type == token::NUMBER) {
        int64_t value = 0;
        auto lexeme = this->get_current().lexeme;
        auto [ptr, ec] = std::from_chars(
            lexeme.data(),
            lexeme.data() + lexeme.size(),
            value
        );
        if (ec == std::errc()) {
            this->consume();
            return value;
        } else {
            return std::unexpected{
                error::make(std::make_error_code(ec).message(), this->get_current())
            };
        }
    }
    return std::unexpected(error::make("Expected number", this->get_current()));
}


std::expected<Json::string, error> parser::parse_string() {
    auto current = this->get_current();
    if (current.type == token::STRING) {
        Json::string str;
        auto lexeme = current.lexeme;
        auto size = lexeme.size() - 1; // Ignore closing quote
        str.reserve(size - 1); // Ignore opening quote
        for (size_t index = 1; index < size; ++index) {
            if (lexeme[index] == '\\') {
                ++index;
                switch (lexeme[index]) {
                    case '"':  str.push_back('"'); break;
                    case '\\': str.push_back('\\'); break;
                    case '/':  str.push_back('/'); break;
                    case 'b':  str.push_back('\b'); break;
                    case 'f':  str.push_back('\f'); break;
                    case 'n':  str.push_back('\n'); break;
                    case 'r':  str.push_back('\r'); break;
                    case 't':  str.push_back('\t'); break;
                    case 'u': {
                        if (index + 4 >= size) {
                            return std::unexpected(error::make("Invalid unicode escape sequence: not enough digits", current));
                        }
                        std::string hex_str(lexeme.substr(index + 1, 4));
                        uint32_t codepoint;
                        auto [ptr, ec] = std::from_chars(hex_str.data(), hex_str.data() + 4, codepoint, 16);
                        if (ec != std::errc()) {
                            return std::unexpected(error::make("Invalid unicode escape sequence: invalid hex", current));
                        }
                        index += 4;
                        
                        // Basic UTF-8 encoding for a single code point from BMP
                        if (codepoint <= 0x7F) {
                            str.push_back(static_cast<char>(codepoint));
                        } else if (codepoint <= 0x7FF) {
                            str.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
                            str.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                        } else { // 0x800 to 0xFFFF
                            str.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
                            str.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                            str.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                        }
                        break;
                    }
                    default:
                        // As per JSON standard, any other escape is an error.
                        // Or we can just push the character as is. For now, let's be strict.
                        return std::unexpected(error::make("Invalid escape character", current));
                }
            } else {
                str.push_back(lexeme[index]);
            }
        }
        this->consume();
        return str;
    }
    return std::unexpected(error::make("Expected string", current));
}

std::expected<Json::boolean, error> parser::parse_boolean() {
    if (this->get_current().type == token::TRUE) {
        this->consume();
        return true;
    } else if (this->get_current().type == token::FALSE) {
        this->consume();
        return false;
    }
    return std::unexpected(error::make("Expected boolean", this->get_current()));
}

std::expected<std::vector<token>, std::string> lex(std::string_view input) {
    auto lines = split_lines(input);
    auto lexer = Json::lexer{};
    auto result = lexer.lex(lines);
    if (result) {
        return result.value();
    }
    return std::unexpected(result.error().to_string(lines));
}
std::expected<Json::json, std::string> parse(std::string_view input) {
    auto lines = split_lines(input);
    auto lexer = Json::lexer{};
    auto result = lexer.lex(lines);
    if (!result) {
        return std::unexpected(result.error().to_string(lines));
    } else {
        auto parser = Json::parser{};
        auto json = parser.parse({result->begin(), result->end()});
        if (!json) {
            return std::unexpected(json.error().to_string(lines));
        }
        return json.value();
    }
}

} // namespace Json