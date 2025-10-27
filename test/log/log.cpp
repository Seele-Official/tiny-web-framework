#include <boost/ut.hpp>
#include <string>
#include <system_error>

#include "log/log.h"


using namespace boost::ut;
namespace {
suite<"Logging Sink Tests"> _ = [] {
    "File Sink"_test = [] {
        constexpr char filename[] = "test.log";
        constexpr char msg[] = "This is a test log message.\n";

        constexpr size_t times = 1000;
        
        {
            seele::log::sink::file file_sink(filename, 1024); // 1KB max size for testing

            for (auto _ : std::views::iota(0uz, times)) {
                file_sink.fmt_to("{}", msg);
            }
        }
        // Verify that the log file was created

        std::error_code ec;
        std::string contents;

        for (auto i : std::views::iota(0uz)) {
            auto candidate = std::format("{}.{}", filename, i);
            if (!std::filesystem::exists(candidate)) {
                break;
            }

            auto file_size = std::filesystem::file_size(candidate);
            expect(file_size <= 8 * 1024) << "Log file exceeded max size" << file_size;

            std::ifstream file(candidate);
            if (!file.is_open()) {
                expect(false) << "Failed to open log file: " << candidate;
                continue;
            }
            contents += std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

            std::filesystem::remove(candidate, ec);
            expect(!ec);
        }
        // Check that the total number of log messages is correct
        size_t count = 0;
        size_t pos = 0;
        while (true) {
            pos = contents.find(msg, pos);
            if (pos == std::string::npos) {
                break;
            }
            ++count;
            pos += sizeof(msg) - 1;
        }
        expect(count == times);
    };
};
} //namespace