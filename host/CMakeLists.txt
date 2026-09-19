cmake_minimum_required(VERSION 3.16)

project(codynex_n1_host LANGUAGES CXX)

add_executable(
    codynex_n1_host
    main.cpp
    ../app/src/main/cpp/n1_core.cpp
    ../app/src/main/cpp/n1_machinery.cpp
    ../app/src/main/cpp/n1_suite.cpp
)

target_include_directories(
    codynex_n1_host
    PRIVATE
    ../app/src/main/cpp
)

target_compile_features(
    codynex_n1_host
    PRIVATE
    cxx_std_17
)

target_compile_options(
    codynex_n1_host
    PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)
