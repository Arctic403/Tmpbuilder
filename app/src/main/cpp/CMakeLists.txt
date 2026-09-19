cmake_minimum_required(VERSION 3.22.1)

project(codynex_n1 LANGUAGES CXX)

add_library(
    codynex_n1
    SHARED
    n1_core.cpp
    n1_machinery.cpp
    n1_suite.cpp
    jni_bridge.cpp
)

target_compile_features(
    codynex_n1
    PRIVATE
    cxx_std_17
)

target_compile_options(
    codynex_n1
    PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)
