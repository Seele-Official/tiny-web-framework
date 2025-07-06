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
}