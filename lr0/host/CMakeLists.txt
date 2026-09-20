cmake_minimum_required(VERSION 3.16)

project(codynex_lr0_host LANGUAGES CXX)

add_executable(
    codynex_lr0_host
    main.cpp
    cxe_builder.cpp
    ../app/src/main/cpp/cxe_format.cpp
    ../app/src/main/cpp/lr0_runtime.cpp
)

target_include_directories(
    codynex_lr0_host
    PRIVATE
    .
    ../app/src/main/cpp
)

target_compile_features(
    codynex_lr0_host
    PRIVATE
    cxx_std_17
)

target_compile_options(
    codynex_lr0_host
    PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)
