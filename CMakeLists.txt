cmake_minimum_required(VERSION 3.15)

project(web_framework LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Library: web_framework ---

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(web_framework ${SOURCES})

set_target_properties(web_framework PROPERTIES CXX_EXTENSIONS OFF)
target_compile_features(web_framework PUBLIC cxx_std_23)

# Set base compile options that will be inherited by consumers

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


set(BASE_COMPILE_FLAGS -Wall -Wextra -fno-exceptions -fno-rtti)

set(BASE_LINK_FLAGS "")


if(BUILD_TESTING)
    message(STATUS "Tests of web_framework are enabled. Configuring test targets...")

    enable_testing()
    
    if (NOT DEFINED BASE_BUILD_TYPE)
        set(BASE_BUILD_TYPE "Release")
    endif()

    if (BASE_BUILD_TYPE STREQUAL "Release")
        message(STATUS "Configuring unit_test for Release build")
        set(BASE_COMPILE_FLAGS ${BASE_COMPILE_FLAGS} -O3)
    elseif(BASE_BUILD_TYPE STREQUAL "Debug")
        message(STATUS "Configuring unit_test for Debug build")
        set(BASE_COMPILE_FLAGS ${BASE_COMPILE_FLAGS} -O0 -g)
    elseif(BASE_BUILD_TYPE STREQUAL "Asan")
        message(STATUS "Configuring unit_test for Asan build")
        set(BASE_COMPILE_FLAGS ${BASE_COMPILE_FLAGS} -O0 -g -fsanitize=address,leak -fno-omit-frame-pointer)
        set(BASE_LINK_FLAGS ${ASAN_LINK_FLAGS} -fsanitize=address,leak)
    elseif(BASE_BUILD_TYPE STREQUAL "Tsan")
        message(STATUS "Configuring unit_test for Tsan build")
        set(BASE_COMPILE_FLAGS ${BASE_COMPILE_FLAGS} -O0 -g -fsanitize=thread)
        set(BASE_LINK_FLAGS ${TSAN_LINK_FLAGS} -fsanitize=thread)
    endif()

    message(STATUS "Compile flags for unit_test: ${BASE_COMPILE_FLAGS}")
    message(STATUS "Link flags for unit_test: ${BASE_LINK_FLAGS}")

    include(FetchContent)
    FetchContent_Declare(
        boost_ut
        GIT_REPOSITORY https://github.com/boost-ext/ut.git
        GIT_TAG        v2.0.1
    )
    FetchContent_MakeAvailable(boost_ut)

    file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test/*.cpp")

    add_executable(unit_test ${TEST_SOURCES})

    target_compile_options(unit_test PRIVATE ${BASE_COMPILE_FLAGS})
    
    target_link_options(unit_test PRIVATE ${BASE_LINK_FLAGS})

    target_link_libraries(unit_test PRIVATE web_framework ut)

    add_test(NAME CppUnitTests COMMAND unit_test)


else()
    message(STATUS "Tests of web_framework are disabled.")
endif()

target_compile_options(web_framework PUBLIC ${BASE_COMPILE_FLAGS})
target_link_options(web_framework PUBLIC ${BASE_LINK_FLAGS})

