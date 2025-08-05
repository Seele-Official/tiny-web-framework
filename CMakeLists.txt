cmake_minimum_required(VERSION 3.15)


project(web_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -fno-exceptions -O3")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -fno-exceptions -O0 -g -fsanitize=address,leak -fno-omit-frame-pointer")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -fno-exceptions -O0 -g -fsanitize=thread -fPIE -pie")
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_subdirectory(lib)

add_executable(web_server ${SOURCES})

find_library(LIBURING_LIBRARY NAMES uring)
if (NOT LIBURING_LIBRARY)
    message(FATAL_ERROR "liburing not found!")
endif()
message(STATUS "Found liburing: ${LIBURING_LIBRARY}")
# link libraries

target_link_libraries(web_server PRIVATE ${LIBURING_LIBRARY})
target_link_libraries(web_server PRIVATE seele)



