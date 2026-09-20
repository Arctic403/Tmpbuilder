cmake_minimum_required(VERSION 3.16)

project(codynex_lr0_host LANGUAGES CXX)

set(
    LR0_COMMON_SOURCES
    cxe_builder.cpp
    ../app/src/main/cpp/cxe_format.cpp
    ../app/src/main/cpp/lr0_runtime.cpp
    ../app/src/main/cpp/lr0_store.cpp
)

add_executable(
    codynex_lr0_host
    main.cpp
    ${LR0_COMMON_SOURCES}
)

add_executable(
    codynex_lr0_recovery
    recovery_main.cpp
    ${LR0_COMMON_SOURCES}
)

foreach(
    target
    codynex_lr0_host
    codynex_lr0_recovery
)
    target_include_directories(
        ${target}
        PRIVATE
        .
        ../app/src/main/cpp
    )

    target_compile_features(
        ${target}
        PRIVATE
        cxx_std_17
    )

    target_compile_options(
        ${target}
        PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
    )
endforeach()
