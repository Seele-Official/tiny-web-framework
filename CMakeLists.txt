cmake_minimum_required(VERSION 3.15)

project(web_framework LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Library: web_framework ---

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(web_framework ${SOURCES})

set_target_properties(web_framework PROPERTIES CXX_EXTENSIONS OFF)
target_compile_features(web_framework PUBLIC cxx_std_23)

# Set base compile options that will be inherited by consumers
target_compile_options(web_framework INTERFACE -Wall -Wextra -fno-exceptions -fno-rtti)

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

target_link_libraries(web_framework PRIVATE ${LIBURING_LIBRARY})

# --- Testing: unit_test ---

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

    # --- Build type configurations for unit_test ---
    # Release: -O3
    target_compile_options(unit_test PRIVATE $<$<CONFIG:Release>:-O3>)

    # Debug: -O0 -g
    target_compile_options(unit_test PRIVATE $<$<CONFIG:Debug>:-O0 -g>)

    # Address Sanitizer: -O0 -g -fsanitize=address,leak
    target_compile_options(unit_test PRIVATE $<$<CONFIG:Asan>:-O0 -g -fsanitize=address,leak -fno-omit-frame-pointer>)
    target_link_options(unit_test PRIVATE $<$<CONFIG:Asan>:-fsanitize=address,leak>)

    # Thread Sanitizer: -O0 -g -fsanitize=thread
    target_compile_options(unit_test PRIVATE $<$<CONFIG:Tsan>:-O0 -g -fsanitize=thread>)
    target_link_options(unit_test PRIVATE $<$<CONFIG:Tsan>:-fsanitize=thread>)

    # Set custom build types in cache for IDEs and user convenience
    set(CMAKE_BUILD_TYPE "${CMAKE_BUILD_TYPE}" CACHE STRING "Choose the build type: Release, Debug, Asan, Tsan")

    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Release Debug Asan Tsan)

    target_link_libraries(unit_test PRIVATE web_framework ut)

    add_test(NAME CppUnitTests COMMAND unit_test)
    
    message("Build type for unit_test set to ${CMAKE_BUILD_TYPE}")

else()
    message(STATUS "Tests of web_framework are disabled.")
endif()
