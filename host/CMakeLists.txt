cmake_minimum_required(VERSION 3.16)

project(codynex_n2_host LANGUAGES CXX)

add_executable(
    codynex_n2_host
    main.cpp
    ../app/src/main/cpp/n2_core.cpp
    ../app/src/main/cpp/n2_primitives.cpp
    ../app/src/main/cpp/n2_plan_common.cpp
    ../app/src/main/cpp/n2_graph_plan.cpp
    ../app/src/main/cpp/n2_tape_plan.cpp
    ../app/src/main/cpp/n2_generator.cpp
    ../app/src/main/cpp/n2_checkpoint.cpp
    ../app/src/main/cpp/n2_suite.cpp
    ../app/src/main/cpp/n2_validation.cpp
)

target_include_directories(
    codynex_n2_host
    PRIVATE
    ../app/src/main/cpp
)

target_compile_features(
    codynex_n2_host
    PRIVATE
    cxx_std_17
)

target_compile_options(
    codynex_n2_host
    PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)
