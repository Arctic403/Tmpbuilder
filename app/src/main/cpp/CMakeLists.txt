cmake_minimum_required(VERSION 3.22.1)

project(codynex_lr0 LANGUAGES CXX)

add_library(
    codynex_lr0
    SHARED
    cxe_format.cpp
    lr0_runtime.cpp
    lr0_store.cpp
    jni_bridge.cpp
)

target_compile_features(
    codynex_lr0
    PRIVATE
    cxx_std_17
)

target_compile_options(
    codynex_lr0
    PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)
