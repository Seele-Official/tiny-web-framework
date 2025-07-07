#include "http.h"
#include <vector>
#include <ranges>
#include <algorithm>
#include "coro_io.h"
namespace http {
    using std::literals::operator""s;
    using std::literals::operator""ms;
    coro::co_task<std::optional<parse_res>, std::string_view> req_msg::parser() {
        enum class parse_state {
            REQUEST_LINE,
            HEADERS,
            BODY,
            DONE
        };

        parse_state state = parse_state::REQUEST_LINE;
        req_msg message;
        size_t content_length = 0;
        std::string body_builder;
        std::string_view buffer;
        size_t start_pos = 0;  


        co_wait_message buffer;

        while (true) {
            switch (state) {
                case parse_state::REQUEST_LINE: {
                    // 查找请求行结束位置
                    size_t line_end = buffer.find("\r\n", start_pos);
                    if (line_end == std::string_view::npos) {
                        // 保存未解析数据位置并等待更多数据
                        start_pos = buffer.size();
                        co_wait_message buffer;
                        continue;
                    }

                    // 提取请求行
                    std::string_view request_line = buffer.substr(start_pos, line_end - start_pos);
                    start_pos = line_end + 2;  // 跳过\r\n

                    // 分割方法、路径和版本
                    std::vector<std::string_view> parts;
                    size_t last = 0;
                    for (size_t i = 0; i <= request_line.size(); ++i) {
                        if (i == request_line.size() || std::isspace(request_line[i])) {
                            if (i - last > 0) {
                                parts.push_back(request_line.substr(last, i - last));
                            }
                            last = i + 1;
                        }
                    }

                    // 验证请求行格式
                    if (parts.size() != 3) {
                        co_return std::nullopt;
                    }

                    // 解析HTTP方法
                    auto method_opt = meta::enum_from_string<req_line::method_t>(parts[0]);
                    if (!method_opt) {
                        co_return std::nullopt;
                    }

                    // 存储请求行信息
                    message.req_l = {
                        *method_opt,
                        std::string(parts[1]),
                        std::string(parts[2])
                    };
                    state = parse_state::HEADERS;
                    break;
                }

                case parse_state::HEADERS: {
                    while (true) {
                        // 查找头部行结束位置
                        size_t line_end = buffer.find("\r\n", start_pos);
                        if (line_end == std::string_view::npos) {
                            start_pos = buffer.size();
                            co_wait_message buffer;
                            continue;
                        }

                        // 检查是否到达头部结束（空行）
                        if (line_end == start_pos) {
                            start_pos = line_end + 2;  // 跳过空行
                            state = parse_state::BODY;
                            break;
                        }

                        // 提取单行头部
                        std::string_view header_line = buffer.substr(start_pos, line_end - start_pos);
                        start_pos = line_end + 2;  // 跳过\r\n

                        // 分割键值对
                        size_t colon_pos = header_line.find(':');
                        if (colon_pos == std::string_view::npos) {
                            co_return std::nullopt;
                        }

                        // 提取并规范化键
                        std::string_view key = header_line.substr(0, colon_pos);
                        while (!key.empty() && std::isspace(key.back())) {
                            key.remove_suffix(1);
                        }

                        // 提取并清理值
                        std::string_view value = header_line.substr(colon_pos + 1);
                        while (!value.empty() && std::isspace(value.front())) {
                            value.remove_prefix(1);
                        }
                        while (!value.empty() && std::isspace(value.back())) {
                            value.remove_suffix(1);
                        }

                        // 存储头部字段
                        message.fields.emplace(key, value);

                        // 记录Content-Length
                        if (key == "Content-Length") {
                            auto [ptr, ec] = std::from_chars(
                                value.data(), 
                                value.data() + value.size(), 
                                content_length
                            );
                            if (ec != std::errc() || ptr != value.data() + value.size()) {
                                co_return std::nullopt;
                            }
                        }
                    }
                    break;
                }

                case parse_state::BODY: {
                    // 无内容主体的情况
                    if (content_length == 0) {
                        state = parse_state::DONE;
                        break;
                    }

                    // 计算剩余需要读取的字节数
                    size_t remaining = content_length - body_builder.size();
                    size_t available = buffer.size() - start_pos;

                    // 复制可用数据到主体构建器
                    if (available > 0) {
                        size_t to_copy = std::min(remaining, available);
                        body_builder.append(buffer.data() + start_pos, to_copy);
                        start_pos += to_copy;
                        remaining -= to_copy;
                    }

                    // 检查是否完成主体读取
                    if (remaining == 0) {
                        message.body = std::move(body_builder);
                        state = parse_state::DONE;
                    } else {
                        co_wait_message buffer;
                    }
                    break;
                }

                case parse_state::DONE: {
                    // 准备返回结果
                    parse_res result{
                        std::move(message),
                        std::nullopt  
                    };
                    // 处理剩余数据
                    if (start_pos < buffer.size()) {
                        result.remain = buffer.substr(start_pos);
                    }

                    co_return result;
                }
            }
        }
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