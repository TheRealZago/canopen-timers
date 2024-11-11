cmake_minimum_required(VERSION 3.13)
project(
    canopen-timers
    LANGUAGES C CXX
)

set(CMAKE_C_FLAGS "-std=c17 -pthread -Wno-psabi")
set(CMAKE_C_FLAGS_DEBUG "-g3 -O0")
set(CMAKE_C_FLAGS_RELEASE "-g0 -O3 -DNDEBUG -s")

set(CMAKE_CXX_FLAGS "-std=c++17 -pthread -Wno-psabi")
set(CMAKE_CXX_FLAGS_DEBUG "-g3 -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-g0 -O3 -DNDEBUG -s")

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/lib/canopen-stack")
list(APPEND PROJ_LIBS canopen-stack)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/lib/socketcan")
list(APPEND PROJ_LIBS socketcan)

list(APPEND PROJ_INCS "${CMAKE_CURRENT_SOURCE_DIR}/lib")

add_executable(canopen-timers
    "src/co_can_linux.cpp"
    "src/co_timer_linux.cpp"
    "src/co_nvm_linux.cpp"
    "src/mystack.cpp"
    "src/varloop.cpp"
    "src/main.cpp"
)

target_include_directories(canopen-timers
    PRIVATE ${PROJ_INCS}
)

target_link_libraries(canopen-timers PRIVATE
    rt
    ${PROJ_LIBS}
)
