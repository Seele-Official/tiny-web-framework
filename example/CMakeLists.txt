cmake_minimum_required(VERSION 3.15)
project(example_consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    web_framework
    GIT_REPOSITORY https://github.com/Seele-Official/tiny-web-framework
    GIT_TAG        main
)

FetchContent_MakeAvailable(web_framework)

add_executable(main example.cpp)

target_compile_options(main PRIVATE $<$<CONFIG:Release>:-O3>)

target_compile_options(main PRIVATE $<$<CONFIG:Debug>:-O0 -g>)

target_compile_options(main PRIVATE $<$<CONFIG:Asan>:-O0 -g -fsanitize=address,leak -fno-omit-frame-pointer>)
target_link_options(main PRIVATE $<$<CONFIG:Asan>:-fsanitize=address,leak>)

target_compile_options(main PRIVATE $<$<CONFIG:Tsan>:-O0 -g -fsanitize=thread>)
target_link_options(main PRIVATE $<$<CONFIG:Tsan>:-fsanitize=thread>)

set(CMAKE_BUILD_TYPE "${CMAKE_BUILD_TYPE}" CACHE STRING "Choose the build type: Release, Debug, Asan, Tsan")

set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Release Debug Asan Tsan)

target_link_libraries(main PRIVATE web_framework)