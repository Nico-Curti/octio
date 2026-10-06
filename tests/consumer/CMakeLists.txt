cmake_minimum_required(VERSION 3.18)
project(octio_consumer LANGUAGES CXX)

find_package(octio CONFIG REQUIRED)

add_executable(octio_consumer main.cpp)
target_link_libraries(octio_consumer PRIVATE octio::octio)
