cmake_minimum_required(VERSION 3.15)
project(example_consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -g -Wall -Wextra -fno-exceptions -fno-rtti")
include(FetchContent)

FetchContent_Declare(
    web_framework
    GIT_REPOSITORY https://github.com/Seele-Official/tiny-web-framework
    GIT_TAG        main
)

FetchContent_MakeAvailable(web_framework)

add_executable(main example.cpp)

target_link_libraries(main PRIVATE web_framework)