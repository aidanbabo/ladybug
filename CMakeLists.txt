cmake_minimum_required(VERSION 3.25)
project(ladybug)

find_package(Freetype REQUIRED)
find_package(Fontconfig REQUIRED)
find_package(harfbuzz CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(unofficial-skia CONFIG REQUIRED)

add_executable(ladybug main.cpp)

target_link_libraries(ladybug PRIVATE z ssl crypto)

target_link_libraries(ladybug PRIVATE Freetype::Freetype)
target_link_libraries(ladybug PRIVATE Fontconfig::Fontconfig)
target_link_libraries(ladybug PRIVATE harfbuzz::harfbuzz)
target_link_libraries(ladybug PRIVATE SDL3::SDL3)
target_link_libraries(ladybug PRIVATE unofficial::skia::skia)
