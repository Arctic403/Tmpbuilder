cmake_minimum_required(VERSION 3.16)

project(codynex_n0_host LANGUAGES CXX)

add_executable(
    codynex_n0_host
    main.cpp
    ../app/src/main/cpp/codynex_n0.cpp
)

target_include_directories(
    codynex_n0_host
    PRIVATE
    ../app/src/main/cpp
)

target_compile_features(
    codynex_n0_host
    PRIVATE
    cxx_std_17
)

target_compile_options(
    codynex_n0_host
    PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)
