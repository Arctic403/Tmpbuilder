cmake_minimum_required(VERSION 3.16)

project(codynex_r0_workspace_records_host LANGUAGES CXX)

add_executable(
    codynex_r0_workspace_records_host
    main.cpp
    ../src/r0_identity.cpp
)

target_include_directories(
    codynex_r0_workspace_records_host
    PRIVATE
    ../include
)

target_compile_features(
    codynex_r0_workspace_records_host
    PRIVATE
    cxx_std_17
)

target_compile_options(
    codynex_r0_workspace_records_host
    PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)
