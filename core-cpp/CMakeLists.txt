cmake_minimum_required(VERSION 3.16)
project(wled_pc_rgb_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Phase-0 spike: a dependency-free probe that connects to a running OpenRGB
# SDK server (default 127.0.0.1:6742) and lists the detected controllers.
# No Qt / no external libs yet — those arrive in Phase 1.
add_executable(orgb_probe src/main.cpp)

if(WIN32)
    target_link_libraries(orgb_probe PRIVATE ws2_32)
endif()
