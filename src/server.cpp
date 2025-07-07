#include <algorithm>
#include <bits/types/struct_iovec.h>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <exception>
#include <expected>
#include <format>
#include <mutex>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <cstddef>
#include <unordered_map>
#include <utility>
#include <vector>
#include <filesystem>

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/sendfile.h>
#include <sys/mman.h>

#include "meta.h"
#include "net/ipv4.h"
#include "coro/task.h"
#include "coro/async.h"
#include "log.h"
#include "http.h"
#include "coro_io.h"
using std::literals::operator""s;
using std::literals::operator""ms;


struct fd_wrapper {
    int fd;
    fd_wrapper(int fd) : fd(fd) {}
    fd_wrapper(fd_wrapper&) = delete;
    fd_wrapper(fd_wrapper&& other) noexcept : fd(other.fd) {
        other.fd = -1; // Prevent the destructor from closing the fd
    }
    fd_wrapper& operator=(fd_wrapper&) = delete;
    fd_wrapper& operator=(fd_wrapper&& other) noexcept {
        if (this != &other) {
            if (is_valid()) {
                close(fd);
            }
            fd = other.fd;
            other.fd = -1; // Prevent the destructor from closing the fd
        }
        return *this;
    }
    operator int() const {
        return fd;
    }

    bool is_valid() const {
        return fd >= 0;
    }
    ~fd_wrapper() {
        if (is_valid()) {
            close(fd);
        }
    }
};


fd_wrapper setup_socket(net::ipv4 v4){
    fd_wrapper fd_w = socket(PF_INET, SOCK_STREAM, 0);
    sockaddr_in addr = v4.to_sockaddr_in();
    if (bind(fd_w, (sockaddr*)&addr, sizeof(addr)) < 0) {
        return fd_wrapper(-1);
    }
    if (listen(fd_w, SOMAXCONN) < 0) {
        return fd_wrapper(-1);
    }
    return fd_w;
}


struct iovec_wrapper : iovec {
    iovec_wrapper(size_t len) : iovec{new std::byte[len], len} {}
    iovec_wrapper(iovec_wrapper&) = delete;
    iovec_wrapper(iovec_wrapper&& other) noexcept : iovec{other.iov_base, other.iov_len} {
        other.iov_base = nullptr;
        other.iov_len = 0;
    }
    iovec_wrapper& operator=(iovec_wrapper&) = delete;
    iovec_wrapper& operator=(iovec_wrapper&& other) noexcept {
        if (this != &other) {
            delete[] static_cast<std::byte*>(iov_base);
            iov_base = other.iov_base;
            iov_len = other.iov_len;
            other.iov_base = nullptr;
            other.iov_len = 0;
        }
        return *this;
    }

    ~iovec_wrapper() {
        if (iov_base){
            delete[] static_cast<std::byte*>(iov_base);
        }
    }
};

enum error_code{
    bad_request = 400,
    forbidden = 403,
    not_found = 404,
    method_not_allowed = 405,





    internal_server_error = 500
};

struct error{
    size_t index;
    std::string_view str;
};

struct error_map{
    std::array<std::string_view, 900> map;

    template<typename ... args_t>
    requires (std::is_same_v<std::decay_t<args_t>, error> && ...)
    consteval error_map(args_t&&... args){
        ((map[args.index] = args.str), ...);
    }
    auto operator[](error_code code) const -> std::string_view {
        return map[static_cast<size_t>(code)];
    }
};

error_map error_contents = {
    error{
        bad_request,
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
    error{
        forbidden,
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
    error{
        not_found,
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
    error{
        method_not_allowed,
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
    error{
        internal_server_error,
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


struct send_http_error : io_link_timeout_awaiter<io_write_awaiter> {

    send_http_error(int fd, error_code code) 
        : io_link_timeout_awaiter<io_write_awaiter>{
            io_write_awaiter{fd, error_contents[code].data(), meta::safe_cast<unsigned int>(error_contents[code].size())},
            5s
        } {}
};

int64_t get_file_size(const fd_wrapper& fd_w) {
    struct stat st;

    if(fstat(fd_w, &st) < 0) {
        return -1;
    }
    if (S_ISBLK(st.st_mode)) {
        int64_t bytes;
        if (ioctl(fd_w, BLKGETSIZE64, &bytes) != 0) {
            return -1;
        }
        return bytes;
    } else if (S_ISREG(st.st_mode))
        return st.st_size;

    return -1;
}





std::string_view default_path = "./";


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



struct file_mmap{
    size_t size;
    void* data;
    file_mmap(size_t length, int prot = PROT_READ | PROT_WRITE,
                      int flags = MAP_SHARED | MAP_ANONYMOUS, int fd = -1,
                      off_t offset = 0) : size(length), data(mmap(nullptr, length, prot, flags, fd, offset)) {
        if (data == MAP_FAILED) {
            std::println("Failed to mmap file: {}", strerror(errno));
            std::terminate();
        }
    }

    file_mmap(const file_mmap&) = delete;
    file_mmap(file_mmap&& other) noexcept : size(other.size), data(other.data) {
        other.data = nullptr;
        other.size = 0;
    }
    file_mmap& operator=(file_mmap&& other) noexcept {
        if (this != &other) {
            if (data) {
                munmap(data, size);
            }
            size = other.size;
            data = other.data;
            other.data = nullptr;
            other.size = 0;
        }
        return *this;
    }

    file_mmap& operator=(const file_mmap&) = delete;

    ~file_mmap(){
        if (data) {
            munmap(data, size);
        }
    }
};

std::unordered_map<std::string, file_mmap> file_caches;
std::mutex file_caches_mutex;
struct get_res{
    iovec_wrapper header;
    iovec data;
};

std::expected<get_res, error_code> handle_get_req(const http::req_msg& req) {

    std::filesystem::path uri_path = req.req_l.uri;
    if (uri_path.empty() || uri_path == "/") {
        uri_path = "/index.html";
    }
    
    uri_path = uri_path.lexically_normal();
    

    if (!uri_path.is_absolute() || 
        uri_path.string().find("..") != std::string::npos) {        return std::unexpected{error_code::forbidden};
    }
    

    std::filesystem::path root_path(default_path);
    std::filesystem::path full_path = root_path / uri_path.relative_path();
    full_path = std::filesystem::weakly_canonical(full_path);


    if (full_path.string().find(root_path.string()) != 0) {
        return std::unexpected{error_code::forbidden};
    }
    

    if (std::filesystem::is_directory(full_path)) {
        full_path /= "index.html";
        
        if (!std::filesystem::exists(full_path)) {
            return std::unexpected{error_code::not_found};
        }
    }
    
    if (!std::filesystem::exists(full_path)) {
        return std::unexpected{error_code::not_found};
    }

    if (std::filesystem::is_directory(full_path)) {
        return std::unexpected{error_code::forbidden};
    }

    if (!std::filesystem::is_regular_file(full_path)) {
        return std::unexpected{error_code::forbidden};
    }

    fd_wrapper file_fd_w(open(full_path.c_str(), O_RDONLY));
    if (!file_fd_w.is_valid()) {
        if (errno == EACCES || errno == EPERM) {
            return std::unexpected{error_code::forbidden};
        }
        return std::unexpected{error_code::not_found};
    }


    int64_t file_size = get_file_size(file_fd_w);
    if (file_size < 0) {
        log::async().error("Failed to get file size for {}", full_path.string());
        return std::unexpected{error_code::internal_server_error};
    }

    get_res res{
        {256},
        {}
    };
    {
        std::lock_guard<std::mutex> lock(file_caches_mutex);
        auto it = file_caches.find(full_path.string());
        if (it != file_caches.end()) {
            res.data.iov_base = it->second.data;
            res.data.iov_len = it->second.size;
        } else {
            file_caches.emplace(
                full_path.string(),
                file_mmap(file_size, PROT_READ, MAP_SHARED, file_fd_w, 0)
            );
        }
    }

    std::string content_type = "application/octet-stream";
    std::string ext = full_path.extension().string();

    if (auto it = mime_types.find(ext);it != mime_types.end()) {
        content_type = it->second;
    }
    

    http::res_msg res_msg{
        {
            .status_code = 200,
            .version = "HTTP/1.1",
            .reason_phrase = "OK"
        },
        {
            {"Content-Length", std::to_string(file_size)},
            {"Content-Type", content_type},
            {"X-Content-Type-Options", "nosniff"} 
        },
        std::nullopt
    };



    res.header.iov_len = meta::safe_cast<size_t>(
        res_msg.format_to(static_cast<char*>(res.header.iov_base))
         - static_cast<char*>(res.header.iov_base)
    );
    return res;
}



std::list<auto (*)(const std::string&) -> bool> listeners = {};


coro::task async_handle_connection(int fd, net::ipv4 addr) {
    fd_wrapper fd_w(fd);
    net::ipv4 client_addr = addr;
    log::async().info("Accepted connection from {}", client_addr.toString());
    co_await coro::thread::dispatch_awaiter{};

    char read_buffer[8192];
    std::optional<std::string_view> remain = std::nullopt;
    while (true) {
        auto parser = http::req_msg::parser();
        if (remain.has_value()) {
            parser.send_and_resume(remain.value());
            remain = std::nullopt;
        }
        while (!parser.done()) {       
            auto res = co_await io_link_timeout_awaiter{
                io_read_awaiter{fd_w, read_buffer, sizeof(read_buffer)},
                60s
            };
            
            if (!res.has_value()) {
                log::async().warn("Read timed out or failed");
                co_return;
            }

            auto bytes_read = res.value().res;
            if (bytes_read <= 0) {
                log::async().warn("Connection closed by client or error occurred");
                co_return;
            }
            parser.send_and_resume({read_buffer, static_cast<size_t>(bytes_read)});
        }

        if (auto result = parser.get()) {
            auto& [msg, remain_opt] = result.value();
            remain = remain_opt;

            switch (msg.req_l.method) {
                case http::req_line::method_t::GET: {
                    auto get_res = handle_get_req(msg);
                    if (get_res.has_value()) {

                        auto res = co_await io_link_timeout_awaiter{
                            io_writev_awaiter{
                                fd_w,
                                &get_res.value().header,
                                2
                            },
                            5s
                        };

                        if (!res.has_value()) {
                            log::async().error("Failed to send response");
                            co_return;
                        }

                    } else {
                        co_await send_http_error{fd_w, get_res.error()};
                    }
                }
                break;
                default: {
                    co_await send_http_error{fd_w, error_code::method_not_allowed};
                    co_return;
                }
            
            }
        } else {
            co_await send_http_error{fd_w, error_code::bad_request};
            co_return;            
        }

    }
}



coro::task server_loop(net::ipv4 addr) {
    auto fd_w = setup_socket(addr);

    if (!fd_w.is_valid()){
        log::async().error("Failed to create socket");
        co_return;
    }
    log::async().info("Server listening on {}", addr.toString());
    co_await coro::thread::dispatch_awaiter{};

    while(true){
        sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

        auto res = co_await io_link_timeout_awaiter{
            io_accept_awaiter{fd_w, (sockaddr *)&client_addr, &client_addr_len},
            5s
        };

        if (!res.has_value()){
            log::async().info("Accept timed out or failed");
            continue;
        }

        async_handle_connection(res.value().res, net::ipv4::from_sockaddr_in(client_addr));
    }

    co_return;
}