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


set(RELEASE_COMPILE_FLAGS "-O3")
set(DEBUG_COMPILE_FLAGS "-O0 -g")
set(ASAN_COMPILE_FLAGS "-O0 -g -fsanitize=address,leak -fno-omit-frame-pointer")
set(TSAN_COMPILE_FLAGS "-O0 -g -fsanitize=thread")


set(CMAKE_BUILD_TYPE "${CMAKE_BUILD_TYPE}" CACHE STRING "Choose the build type: Release, Debug, Asan, Tsan")

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${RELEASE_COMPILE_FLAGS}")
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${DEBUG_COMPILE_FLAGS}")
elseif(CMAKE_BUILD_TYPE STREQUAL "Asan")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${ASAN_COMPILE_FLAGS}")
elseif(CMAKE_BUILD_TYPE STREQUAL "Tsan")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${TSAN_COMPILE_FLAGS}")
endif()


set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Release Debug Asan Tsan)



add_executable(main example.cpp)

target_link_libraries(main PRIVATE web_framework)