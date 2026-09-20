cmake_minimum_required(VERSION 3.22.1)

project(codynex_n2 LANGUAGES CXX)

add_library(
    codynex_n2
    SHARED
    n2_core.cpp
    n2_primitives.cpp
    n2_plan_common.cpp
    n2_graph_plan.cpp
    n2_tape_plan.cpp
    n2_generator.cpp
    n2_checkpoint.cpp
    n2_suite.cpp
    jni_bridge.cpp
)

target_compile_features(
    codynex_n2
    PRIVATE
    cxx_std_17
)

target_compile_options(
    codynex_n2
    PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)
