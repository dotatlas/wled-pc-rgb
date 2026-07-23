cmake_minimum_required(VERSION 3.19)
project(wled_pc_rgb LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Phase-0 spike -----------------------------------------------------------
# Dependency-free probe that connects to a running OpenRGB SDK server
# (default 127.0.0.1:6742) and lists detected controllers. Builds with any
# compiler; no Qt required — handy as a standalone diagnostic.
add_executable(orgb_probe src/main.cpp)
if(WIN32)
    target_link_libraries(orgb_probe PRIVATE ws2_32)
endif()

# --- Phase-1 GUI app ---------------------------------------------------------
# The real application (Qt6 Widgets). Built only when Qt6 is present, so the
# probe still configures/builds on a machine without Qt installed.
find_package(Qt6 QUIET COMPONENTS Widgets Network)
if(Qt6_FOUND)
    message(STATUS "Qt6 found (${Qt6_VERSION}) — building the wled_pc_rgb GUI app.")
    add_subdirectory(app)
else()
    message(STATUS "Qt6 not found — skipping the GUI app. Install qt6-base to build it.")
endif()
