cmake_minimum_required(VERSION 3.22.1)

project(codynex_n0 LANGUAGES CXX)

add_library(
    codynex_n0
    SHARED
    codynex_n0.cpp
    jni_bridge.cpp
)

target_compile_features(
    codynex_n0
    PRIVATE
    cxx_std_17
)

target_compile_options(
    codynex_n0
    PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)
