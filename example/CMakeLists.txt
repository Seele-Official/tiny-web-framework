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

file(GLOB_RECURSE EXAMPLE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/example/*.cpp")

add_executable(example ${EXAMPLE_SOURCES})

target_link_libraries(example PRIVATE web_framework)