#include <bit>
#include <boost/ut.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include "json/json.h"
namespace  {
using namespace boost::ut;

suite<"Data Types"> data_types = [] {
    "Empty Input"_test = [] {
        auto result = Json::parse("");

        expect(!result.has_value());
    };

    "Null"_test = [] {
        auto result = Json::parse( R"(null)");
        expect(result.has_value() && result->as<Json::null>().has_value());
    };

    "Boolean"_test = [] {
        auto t = Json::parse( R"(true)");
        auto f = Json::parse( R"(false)");
        expect(t.has_value() && t->as<Json::boolean>().has_value());
        expect(f.has_value() && f->as<Json::boolean>().has_value());
        expect(t.value() == Json::json(true));
        expect(f.value() == Json::json(false));
    };

    "Number"_test = [] {
        auto result = Json::parse( R"(123)");
        expect(result.has_value());
        auto number = result->as<Json::number>();
        expect(number.has_value());
        expect(number->get() == Json::number(123));
    };

    "String"_test = [] {
        auto result = Json::parse( R"("Hello, World!")");
        expect(result.has_value());
        auto str = result->as<Json::string>();
        expect(str.has_value());
        expect(str->get() == Json::string("Hello, World!"));
    };

    "String with Escapes"_test = [] {
        auto result = Json::parse( R"("Hello, \"World!\"")");
        expect(result.has_value());
        auto str = result->as<Json::string>();
        expect(str.has_value());
        expect(str->get() == Json::string("Hello, \"World!\""));
    };

    "String with Unicode"_test = [] {
        auto result = Json::parse( R"("Hello, \u4F60\u597D")");
        expect(result.has_value());
        auto str = result->as<Json::string>();
        expect(str.has_value());
        uint8_t expected_bytes[] = {
            0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x2C, 0x20,
            0xE4, 0xBD, 0xA0,     // 你
            0xE5, 0xA5, 0xBD,  // 好
        };

        Json::string expected_str;
        for (auto byte : expected_bytes) {
            expected_str.push_back(std::bit_cast<char>(byte));
        }

        expect(str->get() == expected_str);
    };

    "Empty String"_test = [] {
        auto result = Json::parse( R"("")");
        expect(result.has_value());
        auto str = result->as<Json::string>();
        expect(str.has_value());
        expect(str->get() == Json::string(""));
    };

    "Line Break String"_test = [] {
        auto result = Json::parse("Hello, \nWorld!");
        expect(!result.has_value());
    };

};

suite<"Basic Syntax"> basic_syntax = [] {
    "Valid Empty Object"_test = [] {
        auto result = Json::parse( R"({})");
        expect(result.has_value());
        expect(result->as<Json::object>().has_value());
    };

    "Valid Empty Array"_test = [] {
        auto result = Json::parse( R"([])");
        expect(result.has_value());
        expect(result->as<Json::array>().has_value());
    };

    "Simple object"_test = [] {
        auto result = Json::parse( R"({"key": "value"})");
        expect(result.has_value());
        auto obj = result->as<Json::object>();
        expect(obj.has_value());
        expect(obj->get().at("key") == Json::json("value"));
    };

    "Simple Array"_test = [] {
        auto result = Json::parse( R"([1, 2 ,3])");
        expect(result.has_value());
        auto array = result->as<Json::array>();
        expect(array.has_value());
        expect(array->get().size() == 3);
        expect(array->get() == Json::array{1, 2, 3});
    };

    "Error: Missing Closing Brace"_test = [] {
        auto result = Json::parse( R"({"key": "value")");
        expect(!result.has_value());
    };

    "Error: Missing Closing Bracket"_test = [] {
        auto result = Json::parse( R"([1, 2, 3)");
        expect(!result.has_value());
    };

    "Error: Missing Colon"_test = [] {
        auto result = Json::parse( R"({"key" "value"})");
        expect(!result.has_value());
    };

    "Error: Missing Array Comma"_test = [] {
        auto result = Json::parse( R"([1 2, 3])");
        expect(!result.has_value());
    };

    "Error: Missing Object Comma"_test = [] {
        auto result = Json::parse( R"({"key": "value""key2": "value2"})");
        expect(!result.has_value());
    };

    "Error: Missing String Quotes"_test = [] {
        auto result = Json::parse( R"("123)");
        expect(!result.has_value());
    };

    "Error: Using Single Quotes"_test = [] {
        auto result = Json::parse( R"({'key': 'value'})");
        expect(!result.has_value());
    };

    /*Note:
        According to the JSON specification, this two cases are invalid.
        However, some JSON parsers allow trailing commas in objects and arrays,
        because they can make it easier to implement parsers.
    */ 
    "Warning: Trailing Comma in Object"_test = [] {
        auto result = Json::parse( R"({"key": "value",})");
        expect(!result.has_value());
    };
    "Warning: Trailing Comma in Array"_test = [] {
        auto result = Json::parse( R"([1, 2, 3,])");
        expect(!result.has_value());
    };

};

suite<"Nested Structures"> nested_structures = [] {
    "Object in Object"_test = [] {
        auto result = Json::parse( R"({"key": {"nested_key": "nested_value"}})");
        expect(result.has_value());
        auto obj = result->as<Json::object>();
        expect(obj.has_value());

        expect(obj->get().at("key") == Json::json(Json::object{{"nested_key", "nested_value"}}));
    };

    "Array in Object"_test = [] {
        auto result = Json::parse( R"({"key": [1, 2, 3]})");
        expect(result.has_value());
        auto obj = result->as<Json::object>();
        expect(obj.has_value());
        expect(obj->get().at("key") == Json::json(Json::array{1, 2, 3}));
    };

    "Object in Array"_test = [] {
        auto result = Json::parse( R"([{"key": "value"}])");

        auto array = result->as<Json::array>();
        expect(array.has_value());
        expect(array->get().front() == Json::json(Json::object{{"key", "value"}}));
    };

    "Complex Nesting"_test = [] {
        auto result = Json::parse( R"({"array": [1, {"key": "value"}, [2, 3]], "null_value": null})");
        expect(result.has_value());
        auto obj = result->as<Json::object>();
        expect(obj.has_value());
        expect(obj->get().at("array") == Json::json(Json::array{
            1,
            Json::object{{"key", "value"}},
            Json::array{2, 3}
        }));
        expect(obj->get().at("null_value") == Json::json(Json::null{}));
    };
};
    
} //namespace