#include "http.h"
#include <array>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
#include "coro/co_task.h"
#include "meta.h"
#include "math.h"
namespace http {
    using std::literals::operator""s;
    using std::literals::operator""ms;

    constexpr std::array<bool, 256> gen_tchar_map(){
        std::array<bool, 256> tchar_map{};
        for (auto& c :"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz!#$%&'*+-.^_`|~"){
            tchar_map[static_cast<unsigned char>(c)] = true;
        }
        return tchar_map;
    }

    bool is_tchar(char c) {
        static constexpr std::array<bool, 256> tchar_map = gen_tchar_map();
        return tchar_map[static_cast<unsigned char>(c)];
    }


    constexpr char CR = '\r';
    constexpr char LF = '\n';
    constexpr char SP = ' ';
    constexpr char HTAB = '\t';

    constexpr std::string_view CRLF = "\r\n";


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

    template <typename lambda_t>
        requires std::is_same_v<bool, std::invoke_result_t<lambda_t, char>>
    std::string_view parse_token(std::string_view str, lambda_t&& is_valid) {
        size_t end = 0;

        for (auto c:str){
            if (is_valid(c)) {
                end++;
            } else {
                break;
            }
        }
        return str.substr(0, end);
    }

    std::string_view trim_string_view(std::string_view str) {
        size_t start = 0;
        while (start < str.size() && (str[start] == SP || str[start] == HTAB)) {
            ++start;
        }
        size_t end = str.size();
        while (end > start && (str[end - 1] == SP || str[end - 1] == HTAB)) {
            --end;
        }
        return str.substr(start, end - start);
    }

    coro::co_task<std::optional<parse_res>, std::string_view> req_msg::parser() {
        req_msg message;

        std::string_view data;
        co_wait_message data;


        std::string line_buffer{};
        std::string_view line_view{};

        #define get_line()                                                              \
        if (auto line_end = data.find(CRLF); line_end != std::string_view::npos){       \
            line_view = data.substr(0, line_end);                                       \
            data.remove_prefix(line_end + CRLF.size());                                 \
        } else {                                                                        \
            line_buffer.append(data);                                                   \
            while (true) {                                                              \
                co_wait_message data;                                                   \
                auto line_end = data.find(CRLF);                                        \
                if (line_end == std::string_view::npos) {                               \
                    /*If we don't find a complete line, wait for more data*/            \
                    line_buffer.append(data);                                           \
                } else {                                                                \
                    line_buffer.append(data.substr(0, line_end));                       \
                    data.remove_prefix(line_end + CRLF.size());                         \
                    line_view = line_buffer;                                            \
                    break;                                                              \
                }                                                                       \
            }                                                                           \
        }

        // Parse request line
        get_line()

        auto req_line_parts = split_string_view(line_view, SP);
        if (req_line_parts.size() != 3) {
            co_return std::nullopt;
        }
        auto method_opt = meta::enum_from_string<method_t>(req_line_parts[0]);
        if (!method_opt) {
            co_return std::nullopt;
        }

        message.req_l = {
            *method_opt,
            std::string(req_line_parts[1]),
            std::string(req_line_parts[2])
        };

        line_buffer.clear();


        // Parse headers
        while (true) {
            if (data.starts_with(CRLF)) {
                data.remove_prefix(CRLF.size());
                break;
            }
            get_line()
            auto key = parse_token(line_view, is_tchar);
            line_view.remove_prefix(key.size());
            if (key.empty()) {
                co_return std::nullopt;
            }
            if (line_view.empty() || line_view.front() != ':') {
                co_return std::nullopt; // Invalid header format
            }
            line_view.remove_prefix(1); // Skip ':'

            auto value = parse_token(line_view, [](char c){return is_tchar(c) || c == ' ';});
            line_view.remove_prefix(value.size());
            if (value.empty()) {
                co_return std::nullopt; // Invalid header format
            }

            message.fields.emplace(trim_string_view(key), trim_string_view(value));
            line_buffer.clear();
        }

        // Parse body if Content-Length is present
        std::string body_buffer;
        if(auto content_length_it = message.fields.find("Content-Length"); content_length_it != message.fields.end()) {
            if (auto res = math::stoi(content_length_it->second); res.has_value()){
                size_t content_length = res.value();
                if (content_length > 0) {
                    if (data.size() < content_length) {
                        // Not enough data for body, wait for more
                        body_buffer.append(data);
                        co_wait_message data;
                    }
                    message.body = std::move(body_buffer);
                    data.remove_prefix(content_length);
                } else {
                    message.body = std::nullopt; // No body
                }
            } else {
                co_return std::nullopt; // Invalid Content-Length value
            }
        } else {
            message.body = std::nullopt;
        }

        co_return parse_res{
            std::move(message),
            data.empty() ? std::nullopt : std::make_optional(data)
        };

    }


    error_content_map error_contents = {
        {
            error_code::bad_request,
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "X-Content-Type-Options: nosniff\r\n"
            "Connection: close\r\n"
            "\r\n"
            "<!DOCTYPE html>\n"
            "<html>\n"
            "<head>\n"
            "    <title>400 Bad Request</title>\n"
            "    <style>\n"
            "        body { font-family: Arial, sans-serif; line-height: 1.6; margin: 0; padding: 20px; color: #333; }\n"
            "        h1 { color: #d9534f; }\n"
            "        .container { max-width: 800px; margin: 0 auto; }\n"
            "        code { background: #f5f5f5; padding: 2px 4px; }\n"
            "    </style>\n"
            "</head>\n"
            "<body>\n"
            "    <div class=\"container\">\n"
            "        <h1>400 Bad Request</h1>\n"
            "        <p>Your client sent a malformed or illegal request.</p>\n"
            "        <p>Possible causes:</p>\n"
            "        <ul>\n"
            "            <li>Invalid HTTP syntax</li>\n"
            "            <li>Malformed headers</li>\n"
            "            <li>Invalid query parameters</li>\n"
            "        </ul>\n"
            "        <hr>\n"
            "    </div>\n"
            "</body>\n"
            "</html>"
        },
        {
            error_code::forbidden,
            "HTTP/1.1 403 Forbidden\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "X-Content-Type-Options: nosniff\r\n"
            "Connection: close\r\n"
            "\r\n"
            "<!DOCTYPE html>\n"
            "<html>\n"
            "<head>\n"
            "    <title>403 Forbidden</title>\n"
            "    <style>\n"
            "        body { font-family: Arial, sans-serif; line-height: 1.6; margin: 0; padding: 20px; color: #333; }\n"
            "        h1 { color: #d9534f; }\n"
            "        .container { max-width: 800px; margin: 0 auto; }\n"
            "    </style>\n"
            "</head>\n"
            "<body>\n"
            "    <div class=\"container\">\n"
            "        <h1>403 Forbidden</h1>\n"
            "        <p>You don't have permission to access this resource.</p>\n"
            "        <p>Possible reasons:</p>\n"
            "        <ul>\n"
            "            <li>Missing authentication credentials</li>\n"
            "            <li>Insufficient permissions</li>\n"
            "            <li>Path traversal attempt detected</li>\n"
            "        </ul>\n"
            "        <hr>\n"
            "    </div>\n"
            "</body>\n"
            "</html>"    
        },
        {
            error_code::not_found,
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "X-Content-Type-Options: nosniff\r\n"
            "Connection: close\r\n"
            "\r\n"
            "<!DOCTYPE html>\n"
            "<html>\n"
            "<head>\n"
            "    <title>404 Not Found</title>\n"
            "    <style>\n"
            "        body { font-family: Arial, sans-serif; line-height: 1.6; margin: 0; padding: 20px; color: #333; }\n"
            "        h1 { color: #d9534f; }\n"
            "        .container { max-width: 800px; margin: 0 auto; }\n"
            "    </style>\n"
            "</head>\n"
            "<body>\n"
            "    <div class=\"container\">\n"
            "        <h1>404 Not Found</h1>\n"
            "        <p>The requested resource was not found on this server.</p>\n"
            "        <p>Suggestions:</p>\n"
            "        <ul>\n"
            "            <li>Check the URL for typos</li>\n"
            "            <li>Navigate to the <a href=\"/\">home page</a></li>\n"
            "        </ul>\n"
            "        <hr>\n"
            "    </div>\n"
            "</body>\n"
            "</html>"
        },
        {
            error_code::method_not_allowed,
            "HTTP/1.1 405 Method Not Allowed\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Allow: GET\r\n"
            "X-Content-Type-Options: nosniff\r\n"
            "Connection: close\r\n"
            "\r\n"
            "<!DOCTYPE html>\n"
            "<html>\n"
            "<head>\n"
            "    <title>405 Method Not Allowed</title>\n"
            "    <style>\n"
            "        body { font-family: Arial, sans-serif; line-height: 1.6; margin: 0; padding: 20px; color: #333; }\n"
            "        h1 { color: #d9534f; }\n"
            "        .container { max-width: 800px; margin: 0 auto; }\n"
            "    </style>\n"
            "</head>\n"
            "<body>\n"
            "    <div class=\"container\">\n"
            "        <h1>405 Method Not Allowed</h1>\n"
            "        <p>The requested method is not supported for this resource.</p>\n"
            "        <p>Allowed methods: <code>GET</code>, <code>HEAD</code></p>\n"
            "        <hr>\n"
            "    </div>\n"
            "</body>\n"
            "</html>"
        },
        {
            error_code::internal_server_error,
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "X-Content-Type-Options: nosniff\r\n"
            "Connection: close\r\n"
            "\r\n"
            "<!DOCTYPE html>\n"
            "<html>\n"
            "<head>\n"
            "    <title>500 Internal Server Error</title>\n"
            "    <style>\n"
            "        body { font-family: Arial, sans-serif; line-height: 1.6; margin: 0; padding: 20px; color: #333; }\n"
            "        h1 { color: #d9534f; }\n"
            "        .container { max-width: 800px; margin: 0 auto; }\n"
            "        .error-details { \n"
            "            background: #f8f9fa; \n"
            "            border-left: 3px solid #d9534f;\n"
            "            padding: 10px;\n"
            "            margin: 15px 0;\n"
            "            font-family: monospace;\n"
            "            white-space: pre-wrap;\n"
            "            display: none; /* 默认隐藏，可通过JS展开 */\n"
            "        }\n"
            "        .show-details { color: #0066cc; cursor: pointer; }\n"
            "    </style>\n"
            "    <script>\n"
            "        function toggleDetails() {\n"
            "            const el = document.getElementById('error-details');\n"
            "            el.style.display = el.style.display === 'none' ? 'block' : 'none';\n"
            "        }\n"
            "    </script>\n"
            "</head>\n"
            "<body>\n"
            "    <div class=\"container\">\n"
            "        <h1>500 Internal Server Error</h1>\n"
            "        <p>The server encountered an unexpected condition.</p>\n"
            "        <p>Please try again later or contact the administrator.</p>\n"
            "        \n"
            "        <!-- 开发环境可显示错误详情 -->\n"
            "        <span class=\"show-details\" onclick=\"toggleDetails()\">Show technical details</span>\n"
            "        <div id=\"error-details\" class=\"error-details\">\n"
            "            Error ID: #ERR_" /* 可动态插入错误ID或时间戳 */ "\n"
            "            Timestamp: " /* 动态时间戳 */ "\n"
            "        </div>\n"
            "        \n"
            "        <hr>\n"
            "    </div>\n"
            "</body>\n"
            "</html>"   
        }

    };


    std::unordered_map<std::string, std::string> mime_types = {
        // Text and Web Files
        {".html", "text/html"},
        {".htm", "text/html"},
        {".xhtml", "application/xhtml+xml"},
        {".shtml", "text/html"},
        {".txt", "text/plain"},
        {".text", "text/plain"},
        {".log", "text/plain"},
        {".md", "text/markdown"},
        {".markdown", "text/markdown"},
        {".css", "text/css"},
        {".csv", "text/csv"},
        {".rtf", "text/rtf"},

        // Scripts and Code
        {".js", "application/javascript"},
        {".mjs", "application/javascript"},
        {".cjs", "application/javascript"},
        {".json", "application/json"},
        {".jsonld", "application/ld+json"},
        {".xml", "application/xml"},
        {".xsd", "application/xml"},
        {".dtd", "application/xml-dtd"},
        {".plist", "application/xml"},
        {".yaml", "application/yaml"},
        {".yml", "application/yaml"},

        // Images
        {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".jpe", "image/jpeg"},
        {".jfif", "image/jpeg"},
        {".pjpeg", "image/jpeg"},
        {".pjp", "image/jpeg"},
        {".png", "image/png"},
        {".gif", "image/gif"},
        {".bmp", "image/bmp"},
        {".ico", "image/x-icon"},
        {".cur", "image/x-icon"},
        {".svg", "image/svg+xml"},
        {".svgz", "image/svg+xml"},
        {".webp", "image/webp"},
        {".tiff", "image/tiff"},
        {".tif", "image/tiff"},
        {".psd", "image/vnd.adobe.photoshop"},

        // Audio and Video
        {".mp3", "audio/mpeg"},
        {".ogg", "audio/ogg"},
        {".wav", "audio/wav"},
        {".weba", "audio/webm"},
        {".aac", "audio/aac"},
        {".flac", "audio/flac"},
        {".mid", "audio/midi"},
        {".midi", "audio/midi"},
        {".mp4", "video/mp4"},
        {".webm", "video/webm"},
        {".ogv", "video/ogg"},
        {".avi", "video/x-msvideo"},
        {".mov", "video/quicktime"},
        {".wmv", "video/x-ms-wmv"},
        {".flv", "video/x-flv"},
        {".mpeg", "video/mpeg"},
        {".mpg", "video/mpeg"},

        // Archives and Binary
        {".zip", "application/zip"},
        {".rar", "application/x-rar-compressed"},
        {".7z", "application/x-7z-compressed"},
        {".tar", "application/x-tar"},
        {".gz", "application/gzip"},
        {".bz2", "application/x-bzip2"},
        {".xz", "application/x-xz"},
        {".pdf", "application/pdf"},
        {".doc", "application/msword"},
        {".docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
        {".xls", "application/vnd.ms-excel"},
        {".xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
        {".ppt", "application/vnd.ms-powerpoint"},
        {".pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
        {".odt", "application/vnd.oasis.opendocument.text"},
        {".ods", "application/vnd.oasis.opendocument.spreadsheet"},
        {".odp", "application/vnd.oasis.opendocument.presentation"},

        // WebAssembly and Binary Data
        {".wasm", "application/wasm"},
        {".bin", "application/octet-stream"},
        {".exe", "application/octet-stream"},
        {".dll", "application/octet-stream"},
        {".so", "application/octet-stream"},
        {".dmg", "application/octet-stream"},
        {".deb", "application/octet-stream"},
        {".rpm", "application/octet-stream"},

        // Fonts
        {".woff", "font/woff"},
        {".woff2", "font/woff2"},
        {".ttf", "font/ttf"},
        {".otf", "font/otf"},
        {".eot", "application/vnd.ms-fontobject"},

        // Miscellaneous
        {".ics", "text/calendar"},
        {".sh", "application/x-sh"},
        {".php", "application/x-httpd-php"},
        {".swf", "application/x-shockwave-flash"},
        {".apk", "application/vnd.android.package-archive"},
        {".torrent", "application/x-bittorrent"},
        {".epub", "application/epub+zip"}
    };
}