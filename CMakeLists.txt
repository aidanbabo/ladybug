cmake_minimum_required(VERSION 3.25)
project(ladybug)
set(CMAKE_CXX_STANDARD 20)

find_package(Freetype REQUIRED)
find_package(Fontconfig REQUIRED)
find_package(harfbuzz CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(unofficial-skia CONFIG REQUIRED)

add_executable(ladybug src/main.cpp
	src/utils.cpp
	src/network.cpp
	src/layout.cpp
	src/html_parser.cpp
	src/css_parser.cpp
)

target_link_libraries(ladybug PRIVATE z ssl crypto)

target_link_libraries(ladybug PRIVATE Freetype::Freetype)
target_link_libraries(ladybug PRIVATE Fontconfig::Fontconfig)
target_link_libraries(ladybug PRIVATE harfbuzz::harfbuzz)
target_link_libraries(ladybug PRIVATE SDL3::SDL3)
target_link_libraries(ladybug PRIVATE unofficial::skia::skia)

target_compile_options(ladybug PRIVATE -g -Wall -Wextra -Wpedantic)
