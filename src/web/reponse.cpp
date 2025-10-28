
#include <cstdint>
#include <vector>

#include "log/log.h"
#include "web/response.h"
#include "web/routing.h"




namespace web::response {


task msg(const http::response::msg& msg){ 
    std::vector<char> buffer{};

    buffer.reserve(1024 + msg.body.size());

    msg.format_to(std::back_inserter(buffer));

    auto [fd, client_addr, timeout] = co_await task::get_settings{};

    
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
}
    
task error(http::response::status_code code){
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

    auto [fd, client_addr, timeout] = co_await task::get_settings{};

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
}
task file_head(const std::string& content_type, size_t size){
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

    auto [fd, client_addr, timeout] = co_await task::get_settings{};
    co_return co_await io::awaiter::link_timeout{
        io::awaiter::write{ fd, header.data(), (uint32_t) header.size() },
        timeout
    };
}


task file(const std::string& content_type, std::span<std::byte> content){
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

    auto [fd, client_addr, timeout] = co_await task::get_settings{};


    iovec iov[2] = {
        { header.data(), header.size() },
        { content.data(), content.size() }
    };

    size_t total_size = header.size() + content.size();
    size_t sent_size = 0;

    // The first writev attempt, which may write both header and part of content
    int32_t res = co_await io::awaiter::link_timeout{
        io::awaiter::writev{ fd, iov, 2 },
        timeout
    };

    if (res <= 0) {
        log::async::error(
            "Failed to send response for {} : {}", 
            client_addr.to_string(), io::error::msg
        );
        co_return -1;
    }

    sent_size = res;

    // ================== Fast Path ==================
    // Assume the common case where header is fully sent in the first writev
    if (static_cast<size_t>(res) >= header.size()) [[likely]] { 
        
        size_t content_sent = res - header.size();
        size_t content_remaining = content.size() - content_sent;
        
        auto content_ptr = content.data() + content_sent;


        while (content_remaining > 0) {
            res = co_await io::awaiter::link_timeout{
                io::awaiter::write{ fd, content_ptr, (uint32_t) content_remaining },
                timeout
            };

            if (res <= 0) {
                log::async::error(
                    "Failed to send response for {} : {}", 
                    client_addr.to_string(), io::error::msg
                );
                co_return -1;
            }

            content_ptr += res;
            content_remaining -= res;
            sent_size += res;
        }

    } 
    // ================== Slow Path ==================
    // Header not fully sent, need to handle partial header and content
    else {
        iovec* current_iov = iov;
        uint32_t iov_count = 2;
        
        auto written_bytes = static_cast<size_t>(res);

        current_iov->iov_base = static_cast<char*>(current_iov->iov_base) + written_bytes;
        current_iov->iov_len -= written_bytes;


        while (sent_size < total_size) {
            res = co_await io::awaiter::link_timeout{
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
            written_bytes = static_cast<size_t>(res);

            while (written_bytes > 0 && iov_count > 0) {
                size_t bytes_to_consume = std::min(written_bytes, current_iov->iov_len);
                current_iov->iov_base = static_cast<char*>(current_iov->iov_base) + bytes_to_consume;
                current_iov->iov_len -= bytes_to_consume;
                written_bytes -= bytes_to_consume;
                if (current_iov->iov_len == 0) {
                    current_iov++;
                    iov_count--;
                }
            }
        }
    }


    co_return sent_size;
}

task file(const std::string& content_type, const io::mmap& content){
    return response::file(content_type, {content.get_data(), content.get_size()});
}



};

