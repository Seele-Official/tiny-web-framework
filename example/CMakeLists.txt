cmake_minimum_required(VERSION 3.15)
project(example_consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Release Debug Asan Tsan)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -g")
elseif(CMAKE_BUILD_TYPE STREQUAL "Asan")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -g -fsanitize=address,leak -fno-omit-frame-pointer")
elseif(CMAKE_BUILD_TYPE STREQUAL "Tsan")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -g -fsanitize=thread")
endif()


include(FetchContent)

FetchContent_Declare(
    web_framework
    GIT_REPOSITORY https://github.com/Seele-Official/tiny-web-framework
    GIT_TAG        main
)

FetchContent_MakeAvailable(web_framework)



add_executable(main example.cpp)

target_compile_options(main PRIVATE ${CMAKE_CXX_FLAGS})

target_link_libraries(main PRIVATE web_framework)