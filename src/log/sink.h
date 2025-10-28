#pragma once
#include <cstddef>
#include <cstdio>
#include <exception>
#include <iostream>
#include <mutex>
#include <print>
#include <ranges>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

#include "common.h"
#include "meta.h"

namespace log::sink {


namespace detail {
class fmt_ostream {
public:
    class flush_iterator {
    public:
        using iterator_category = std::output_iterator_tag;
        using value_type = char;
        using difference_type = std::ptrdiff_t;
        using pointer = char*;
        using reference = char&;

        explicit flush_iterator(fmt_ostream* parent) : parent(parent) {}

        flush_iterator& operator=(char c) {
            if (parent->current == parent->end) {
                parent->flush();
            }
            *parent->current++ = c;
            return *this;
        }

        flush_iterator& operator*() { return *this; }
        flush_iterator& operator++() { return *this; }
        flush_iterator operator++(int) { return *this; }

    private:
        fmt_ostream* parent;
    };


    explicit fmt_ostream(size_t size = 8192) 
        : begin(new char[size]), current(begin), end(begin + size) {}

    fmt_ostream(const fmt_ostream&) = delete;
    fmt_ostream& operator=(const fmt_ostream&) = delete;

    fmt_ostream(fmt_ostream&& other) noexcept
        : begin(std::exchange(other.begin, nullptr)),
          current(std::exchange(other.current, nullptr)),
          end(std::exchange(other.end, nullptr)) {}

    fmt_ostream& operator=(fmt_ostream&& other) noexcept {
        if (this != &other) {
            if (begin) {
                flush();
                delete[] begin;
            }
            begin   = std::exchange(other.begin, nullptr);
            current = std::exchange(other.current, nullptr);
            end     = std::exchange(other.end, nullptr);
        }
        return *this;
    }

    ~fmt_ostream() {
        if (current > begin) {
            std::println("log: unflushed data detected in fmt_ostream destructor");
            std::terminate();
        }
        delete[] begin;
    }
    

    template<typename... Args>
    void fmt_to(std::format_string<Args...> fmt, Args&&... args) {
        const size_t write_size = std::formatted_size(fmt, std::forward<Args>(args)...);

        if (remaining() < write_size) {
            // slow path
            std::format_to(flush_iterator{this}, fmt, std::forward<Args>(args)...);
        } else {
            // fast path
            auto result = std::format_to_n(current, remaining(), fmt, std::forward<Args>(args)...);
            current = result.out;
        }
    }

    void flush() {
        if (current > begin) {
            do_flush(begin, current - begin);
            current = begin; 
        }
    }

protected:
    virtual void do_flush(const char* data, size_t count) = 0;

private:
    size_t remaining() const { return end - current; }

    char* begin{nullptr};
    char* current{nullptr};
    char* end{nullptr};
};
}


class basic : public detail::fmt_ostream {
public:
    using detail::fmt_ostream::fmt_ostream;

    virtual ~basic() = default;

    template<level lvl, typename... args_t>
    void log(system_clock::time_point time, format_string_wrapper<args_t...> fmt_w, args_t&&... args){
        std::lock_guard lock{mutex};
        auto [loc, fmt] = fmt_w;

        if (lvl <= this->level) {
            this->fmt_to(
                "[{}] [{:%Y-%m-%d %H:%M:%S}]: `{}` at {}:{}:{}\n",
                meta::enum_name<lvl>(), 
                time,
                std::format(
                    fmt, std::forward<args_t>(args)...
                ),
                loc.file_name(), loc.line(), loc.column()
            );
        }
    }
private:
    log::level level{max_level};
    std::mutex mutex{};
};

class cout final : public basic {
public:
    cout() = default;
    ~cout() final {
        this->flush();
    }
protected:
    void do_flush(const char* data, size_t count) final {
        
        std::cout.write(data, count);
    }
};

class cerr final : public basic {
public:
    cerr() = default;
    ~cerr() final {
        this->flush();
    }
protected:
    void do_flush(const char* data, size_t count) final {
        std::cerr.write(data, count);
    }
};

class file final : public basic {
public:
    file(std::string_view p = "server.log", size_t s = 10 * 1024 * 1024) // default 10MB
        : max_size(s), path(p) 
    {
        this->open_current_file();
    }

    ~file() final {
        this->flush();
        std::fclose(this->file_ptr);
    }

    file(const file&) = delete;
    file& operator=(const file&) = delete;
    file(file&&) = delete;
    file& operator=(file&&) = delete;

protected:
    void do_flush(const char* data, size_t count) final {
        this->size_written += std::fwrite(data, 1, count, this->file_ptr);
        if (this->size_written > max_size) {
            roll_file();
        }
    }

private:
    static FILE* fopen(std::string_view target) {
        std::FILE* new_file   = std::fopen(target.data(), "a");

        if (!new_file) {
            return nullptr;
        }

        if (std::setvbuf(new_file, nullptr, _IONBF, 0) != 0) {
            std::fclose(new_file);
            return nullptr;
        }
        return new_file;
    }

    void open_current_file() {
        for (auto i : std::views::iota(0uz)) {

            auto candidate = std::format("{}.{}", path, i);
            auto candidate_path = std::filesystem::path(candidate);

            std::error_code ec;

            if (!std::filesystem::exists(candidate_path, ec)) {       
                auto parent_path = candidate_path.parent_path();
                if (!parent_path.empty() && !std::filesystem::exists(parent_path, ec)) {
                    std::filesystem::create_directories(parent_path, ec);
                    if (ec) {
                        std::println("Error: Failed to create directories for {}: {}", candidate, ec.message());
                        std::terminate();
                    }
                }

                if (ec) {
                    std::println("Error: Failed to check existence of {}: {}", parent_path.string(), ec.message());
                    std::terminate();
                }

                if (auto f = fopen(candidate); f) {
                    this->file_ptr     = f;
                    this->number       = i;
                    this->size_written = 0;
                    return;
                } else {
                    std::println("Error: Failed to open log file: {}", candidate);
                    std::terminate();
                }
            }

            if (ec) {
                std::println("Error: Failed to check existence of {}: {}", candidate_path.string(), ec.message());
                std::terminate();
            }

            auto current_size = std::filesystem::file_size(candidate_path, ec);
            if (ec) {
                std::println("Error: Failed to get size of {}: {}", candidate, ec.message());
                std::terminate();
            }
            
            if (current_size < max_size) {
                if (auto f = fopen(candidate); f) {
                    this->file_ptr     = f;
                    this->number       = i;
                    this->size_written = current_size;
                    return;
                } else {
                    std::println("Error: Failed to open log file: {}", candidate);
                    std::terminate();
                }
            }
            
        }
    }

    void roll_file() {
        if (this->file_ptr) {
            std::fclose(this->file_ptr);
            this->file_ptr = nullptr;
        }

        ++this->number;

        auto f = fopen(std::format("{}.{}", this->path, this->number));
        if (!f) {
            std::println("Error: Failed to open log file: {}", std::format("{}.{}", this->path, this->number));
            std::terminate();
        }
        this->size_written = 0;
        this->file_ptr     = f;
    }



    std::FILE*  file_ptr{nullptr};
    size_t      max_size{};
    size_t      size_written{0};
    size_t      number{0};
    std::string path{};
};

}