cmake_minimum_required(VERSION 3.13)
project(
    socketcan
    LANGUAGES CXX
)

add_library(socketcan
    "socketcan.cpp"
)

target_compile_definitions(socketcan
    PRIVATE CMAKE_EXPORT_COMPILE_COMMANDS=1
)

target_include_directories(socketcan
    PRIVATE "${CMAKE_SYSROOT}/usr/include/libnl3/"
)

find_library(NL_LIB nl-3 REQUIRED)
find_library(NL_ROUTE_LIB nl-route-3 REQUIRED)
find_library(NL_GENL_LIB nl-genl-3 REQUIRED)

target_link_libraries(socketcan PUBLIC
    ${NL_LIB} ${NL_ROUTE_LIB} ${NL_GENL_LIB}
)
