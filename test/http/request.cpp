#include <boost/ut.hpp>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>
#include <ranges>
#include "http/request.h"

namespace {
using namespace boost::ut;
using namespace http;

suite<"request parser"> _ = []{
    "simple"_test = [] {
        std::string_view raw_request = "GET /test HTTP/1.1\r\nHost: example.com\r\nUser-Agent: TestClient/1.0\r\n\r\n";
        auto parser = request::parser{};

        parser.feed(raw_request);
        
        expect(!parser.empty());
        auto request_opt = parser.pop_front();
        expect(request_opt.has_value());

        request::msg& request = request_opt.value();

        auto [method, target, version] = request.line;

        expect(
            method == request::method::GET
            && version == "HTTP/1.1"
        );


        auto origin = std::get_if<request::origin_form>(&target);

        expect(
            origin != nullptr
            && origin->path == "/test"
            && origin->query.empty()
        );

        expect(request.header.size() == 2);
        expect(request.header["Host"] == "example.com");
        expect(request.header["User-Agent"] == "TestClient/1.0");

        expect(request.body.empty());

    };

    "with query"_test = [] {
        std::string_view raw_request = "GET /search?q=boost HTTP/1.1\r\nHost: example.com\r\n\r\n";
        auto parser = request::parser{};

        parser.feed(raw_request);

        expect(!parser.empty());
        auto request_opt = parser.pop_front();
        expect(request_opt.has_value());

        request::msg& request = request_opt.value();

        auto [method, target, version] = request.line;

        expect(
            method == request::method::GET
            && version == "HTTP/1.1"
        );

        auto origin = std::get_if<request::origin_form>(&target);

        expect(
            origin != nullptr
            && origin->path == "/search"
            && origin->query == "q=boost"
        );

        expect(request.header.size() == 1);
        expect(request.header["Host"] == "example.com");
        expect(request.body.empty());
    };

    "with body"_test = [] {
        std::string_view raw_request = "POST /submit HTTP/1.1\r\nHost: example.com\r\nContent-Length: 11\r\n\r\nHello World";

        auto parser = request::parser{};

        parser.feed(raw_request);

        expect(!parser.empty());
        auto request_opt = parser.pop_front();
        expect(request_opt.has_value());

        request::msg& request = request_opt.value();

        auto [method, target, version] = request.line;

        expect(
            method == request::method::POST
            && std::get<request::origin_form>(target).path == "/submit"
            && version == "HTTP/1.1"
        );

        expect(request.header.size() == 2);
        expect(request.header["Host"] == "example.com");
        expect(request.header["Content-Length"] == "11");

        expect(request.body == "Hello World");
    };

    "asterisk form"_test = [] {
        std::string_view raw_request = "OPTIONS * HTTP/1.1\r\nHost: example.com\r\n\r\n";
        auto parser = request::parser{};

        parser.feed(raw_request);

        expect(!parser.empty());
        auto request_opt = parser.pop_front();
        expect(request_opt.has_value());

        request::msg& request = request_opt.value();

        expect(
            request.line.method == request::method::OPTIONS
            && std::get_if<request::asterisk_form>(&request.line.target)
            && request.line.version == "HTTP/1.1"
        );

        expect(request.header.size() == 1);
        expect(request.header["Host"] == "example.com");
        expect(request.body.empty());
    };

    "incomplete request"_test = [] {
        std::string_view raw_request = "GET /test HTTP/1.1\r\nHost: example.com\r\n";

        auto parser = request::parser{};
    
        parser.feed(raw_request);
        expect(parser.empty());
        parser.feed("\r\n");

        expect(!parser.empty());
        auto request_opt = parser.pop_front();
        expect(request_opt.has_value());

        request::msg& request = request_opt.value();

        expect(
            request.line.method == request::method::GET
            && std::get<request::origin_form>(request.line.target).path == "/test"
            && request.line.version == "HTTP/1.1"
        );

        expect(request.header.size() == 1);
        expect(request.header["Host"] == "example.com");
        expect(request.body.empty());
    };

    "multiple requests"_test = [] {
        std::string_view raw_request = "GET /first HTTP/1.1\r\nHost: example.com\r\n\r\nGET /second HTTP/1.1\r\nHost: example.org\r\n\r\n";
        

        auto parser = request::parser{};

        parser.feed(raw_request);
        expect(!raw_request.empty());

        auto request1_opt = parser.pop_front();
        auto& request1 = request1_opt.value();

        expect(
            request1.line.method == request::method::GET
            && std::get<request::origin_form>(request1.line.target).path == "/first"
            && request1.line.version == "HTTP/1.1"
        );

        expect(request1.header.size() == 1);
        expect(request1.header.contains("Host"));
        expect(request1.header["Host"] == "example.com");
        expect(request1.body.empty());

        expect(!parser.empty());
        auto request2_opt = parser.pop_front();
        auto& request2 = request2_opt.value();

        expect(parser.empty());

        expect(
            request2.line.method == request::method::GET
            && std::get<request::origin_form>(request2.line.target).path == "/second"
            && request2.line.version == "HTTP/1.1"
        );

        expect(request2.header.size() == 1);
        expect(request2.header.contains("Host"));
        expect(request2.header["Host"] == "example.org");
        expect(request2.body.empty());
    };

    "malformed request"_test = [] {

        std::vector<std::string_view> malformed_requests = {
            // Missing HTTP version
            "GET /test\r\nHost: example.com\r\n\r\n",
            // Missing method
            "/test HTTP/1.1\r\nHost: example.com\r\n\r\n",
            // Missing path
            "GET HTTP/1.1\r\nHost: example.com\r\n\r\n",
            // Invalid method
            "FETCH /test HTTP/1.1\r\nHost: example.com\r\n\r\n",
            // Invalid path encoding
            "GET /## st HTTP/1.1\r\nHost: example.com\r\n\r\n",
            // Missing ':' in header
            "GET /test HTTP/1.1\r\nHost example.com\r\n\r\n",
            // Non-numeric Content-Length
            "POST /submit HTTP/1.1\r\nHost: example.com\r\nContent-Length: abc\r\n\r\nHello World",
        };

        auto escape = [](std::string_view sv) {
            return sv
                | std::views::transform([](char c) -> std::string {
                    switch (c) {
                        case '"':  return "\\\"";
                        case '\\': return "\\\\";
                        case '\n': return "\\n";
                        case '\r': return "\\r";
                        case '\t': return "\\t";
                        default:
                            return std::string{c};
                    }
                })
                | std::views::join
                | std::ranges::to<std::string>();
        };

        auto parser = request::parser{};

        for (const auto& raw_request : malformed_requests) {
            request::msg request;
            parser.feed(raw_request);

            expect(!parser.empty());

            expect(!parser.pop_front().has_value()) << "Failed on request: " << escape(raw_request);
        }
    };
};
}