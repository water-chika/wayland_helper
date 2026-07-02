cmake_minimum_required(VERSION 3.20)

project(wayland_helper)

add_library(wayland_helper
    wayland_helper.hpp
    wayland_helper.cpp
)

find_library(wayland-client wayland-client)

target_include_directories(wayland_helper PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(wayland_helper PUBLIC
    wayland-client
    wayland-xdg-shell-client
)

