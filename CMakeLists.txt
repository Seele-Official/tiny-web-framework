cmake_minimum_required(VERSION 3.15)


project(web_framework LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_FLAGS_BASE "-Wall -Wextra -fno-exceptions -fno-rtti")
set(BUILD_MODE "release" CACHE STRING "Build mode (release, debug, asan, tsan)")

if (BUILD_MODE STREQUAL "release")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_BASE} -O3")
elseif (BUILD_MODE STREQUAL "debug")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_BASE} -O0 -g")
elseif (BUILD_MODE STREQUAL "asan")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_BASE} -O0 -g -fsanitize=address,leak -fno-omit-frame-pointer")
elseif (BUILD_MODE STREQUAL "tsan")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_BASE} -O0 -g -fsanitize=thread")
endif()

message(STATUS "Build mode: ${BUILD_MODE}")

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")


add_library(web_framework ${SOURCES})

target_include_directories(web_framework 
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        # $<INSTALL_INTERFACE:include> # For installation, if you need it
)


find_library(LIBURING_LIBRARY NAMES uring)
if (NOT LIBURING_LIBRARY)
    message(FATAL_ERROR "liburing not found!")
endif()
message(STATUS "Found liburing: ${LIBURING_LIBRARY}")
# link libraries

target_link_libraries(web_framework PRIVATE ${LIBURING_LIBRARY})




option(BUILD_TESTING "Build the testing targets" ON)

if(BUILD_TESTING)
    message(STATUS "Tests are enabled. Configuring test targets...")

    enable_testing()

    include(FetchContent)
    FetchContent_Declare(
        boost_ut
        GIT_REPOSITORY https://github.com/boost-ext/ut.git
        GIT_TAG        v2.0.1
    )
    FetchContent_MakeAvailable(boost_ut)

    file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test/*.cpp")

    add_executable(unit_test ${TEST_SOURCES})

    target_link_libraries(unit_test PRIVATE web_framework ut)

    add_test(NAME CppUnitTests COMMAND unit_test)

else()
  message(STATUS "Tests are disabled.")
endif()



option(BUILD_EXAMPLE "Build the example targets" ON)

if(BUILD_EXAMPLE)
    message(STATUS "Examples are enabled. Configuring example targets...")

    file(GLOB_RECURSE EXAMPLE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/example/*.cpp")

    add_executable(example ${EXAMPLE_SOURCES})

    target_link_libraries(example PRIVATE web_framework)
else()
  message(STATUS "Examples are disabled.")
endif()

