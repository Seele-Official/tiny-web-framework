
#include <vector>

#include "log.h"
#include "web/response.h"
#include "web/routing.h"




namespace web::response {
using namespace seele;


task msg(const http::response::msg& msg){ 
    return [](const http::response::msg& msg) -> detail::send_task {
        std::vector<char> buffer{};

        buffer.reserve(1024 + msg.body.size());

        msg.format_to(std::back_inserter(buffer));

        auto [fd, client_addr, timeout] = co_await task::wait_setting{};

        
        size_t total_size = buffer.size();
        size_t sent_size = 0;
        while (sent_size < total_size) {
            auto offset = buffer.data() + sent_size;
            auto remaining_size = total_size - sent_size;
            int32_t res = co_await io::awaiter::link_timeout{
                io::awaiter::write{
                    fd,
                    offset,
                    (uint32_t) remaining_size
                },
                timeout
            };
            if (res <= 0) {
                log::async::error(
                    "Failed to send response for `{}`: `{}`", 
                    client_addr.to_string(), io::error::msg
                );
                co_return -1;
            }
            sent_size += res;
        }
        co_return sent_size;
    }(msg);

}
    
task error(http::response::status_code code){
    return [](http::response::status_code code) -> detail::send_task {
        auto page = routing::env::error_page_provider()(code);

        http::response::msg msg{
            {code},
            {
                {"Content-Type", "text/html; charset=utf-8"},
                {"Content-Length", std::to_string(page.size())},
                {"Connection", "close"},
            }
        };
        std::vector<char> buffer{};
        buffer.reserve(256 + page.size());
        msg.format_to(std::back_inserter(buffer));
        buffer.insert(buffer.end(), page.begin(), page.end());

        auto [fd, client_addr, timeout] = co_await task::wait_setting{};

        size_t total_size = buffer.size();
        size_t sent_size = 0;
        while (sent_size < total_size) {
            auto offset = buffer.data() + sent_size;
            auto remaining_size = total_size - sent_size;
            int32_t res = co_await io::awaiter::link_timeout{
                io::awaiter::write{
                    fd,
                    offset,
                    (uint32_t) remaining_size
                },
                timeout
            };
            if (res <= 0) {
                log::async::error(
                    "Failed to send response for `{}`: `{}`", 
                    client_addr.to_string(), io::error::msg
                );
                co_return -1;
            }
            sent_size += res;
        }
        co_return sent_size;

    }(code);
}
task file_head(const std::string& content_type, size_t size){
    return [](const std::string& content_type, size_t size) -> detail::send_task {
        std::vector<char> header{};
        header.reserve(256);

        http::response::msg msg {
            {http::response::status_code::ok},
            {
                {"Content-Type", content_type},
                {"Content-Length", std::to_string(size)},
                {"Connection", "keep-alive"},
            }
        };
        msg.format_to(std::back_inserter(header));

        auto [fd, client_addr, timeout] = co_await task::wait_setting{};
        co_return co_await io::awaiter::link_timeout{
            io::awaiter::write{ fd, header.data(), (uint32_t) header.size() },
            timeout
        };
    }(content_type, size);
}


task file(const std::string& content_type, std::span<std::byte> content){
    return [](const std::string& content_type, std::span<std::byte> content) -> detail::send_task {
        std::vector<char> header{};
        header.reserve(256);

        http::response::msg msg {
            {http::response::status_code::ok},
            {
                {"Content-Type", content_type},
                {"Content-Length", std::to_string(content.size())},
                {"Connection", "keep-alive"},
            }
        };

        msg.format_to(std::back_inserter(header));

        auto [fd, client_addr, timeout] = co_await task::wait_setting{};


        iovec iov[2] = {
            { header.data(), header.size() },
            { content.data(), content.size() }
        };

        size_t total_size = header.size() + content.size();
        size_t sent_size = 0;


        struct iovec* current_iov = iov;
        uint32_t iov_count = 2;

        while (sent_size < total_size) {
            int32_t res = co_await io::awaiter::link_timeout{
                io::awaiter::writev{ fd, current_iov, iov_count },
                timeout
            };

            if (res <= 0) {
                log::async::error(
                    "Failed to send response for {} : {}", 
                    client_addr.to_string(), io::error::msg
                );
                co_return -1;
            }

            sent_size += res;
            auto written_bytes = static_cast<size_t>(res);

            while (written_bytes > 0 && iov_count > 0) {
                if (written_bytes < current_iov->iov_len) {
                    current_iov->iov_base = static_cast<char*>(current_iov->iov_base) + written_bytes;
                    current_iov->iov_len -= written_bytes;
                    written_bytes = 0; 
                } else {
                    written_bytes -= current_iov->iov_len;
                    current_iov++;
                    iov_count--;
                }
            }
        }


        co_return sent_size;
    }(content_type, content);
}

task file(const std::string& content_type, const io::mmap& content){
    return response::file(content_type, {content.get_data(), content.get_size()});
}



};

