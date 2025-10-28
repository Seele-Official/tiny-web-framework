cmake_minimum_required(VERSION 3.15)


project(web_framework LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(MY_CMAKE_CXX_FLAGS_BASE -Wall -Wextra -fno-exceptions -fno-rtti)
set(WEB_FRAMEWORK_BUILD_MODE "release" CACHE STRING "Build mode (release, debug, asan, tsan)")

if (WEB_FRAMEWORK_BUILD_MODE STREQUAL "release")
    set(MY_CMAKE_CXX_FLAGS ${MY_CMAKE_CXX_FLAGS_BASE} -O3)
elseif (WEB_FRAMEWORK_BUILD_MODE STREQUAL "debug")
    set(MY_CMAKE_CXX_FLAGS ${MY_CMAKE_CXX_FLAGS_BASE} -O0 -g)
elseif (WEB_FRAMEWORK_BUILD_MODE STREQUAL "asan")
    set(MY_CMAKE_CXX_FLAGS ${MY_CMAKE_CXX_FLAGS_BASE} -O0 -g -fsanitize=address,leak -fno-omit-frame-pointer)
elseif (WEB_FRAMEWORK_BUILD_MODE STREQUAL "tsan")
    set(MY_CMAKE_CXX_FLAGS ${MY_CMAKE_CXX_FLAGS_BASE} -O0 -g -fsanitize=thread)
endif()

message(STATUS "Build mode: ${WEB_FRAMEWORK_BUILD_MODE}")

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(web_framework ${SOURCES})

set_target_properties(web_framework PROPERTIES CXX_EXTENSIONS OFF)

target_compile_features(web_framework PUBLIC cxx_std_23)

target_compile_options(web_framework PRIVATE ${MY_CMAKE_CXX_FLAGS})

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



if (CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    option(BUILD_TESTING "Build the testing targets" ON)
else()
    option(BUILD_TESTING "Build the testing targets" OFF)
endif()


if(BUILD_TESTING)
    message(STATUS "Tests of web_framework are enabled. Configuring test targets...")

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
    message(STATUS "Tests of web_framework are disabled.")
endif()
