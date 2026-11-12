cmake_minimum_required(VERSION 3.21)
project(BootHush LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "BootHush currently targets Windows hosts")
endif()

add_library(moira STATIC
    third_party/moira/Moira.cpp
    third_party/moira/MoiraCore68000.cpp
    third_party/moira/MoiraCore68010.cpp
    third_party/moira/MoiraCore68020.cpp
    third_party/moira/MoiraDebugger.cpp
)
target_include_directories(moira SYSTEM PUBLIC third_party/moira)
target_compile_features(moira PUBLIC cxx_std_20)

add_library(i286 STATIC
    third_party/mame-i286/i286.cpp
    third_party/mame-i286/i86.cpp
)
target_include_directories(i286 SYSTEM PUBLIC third_party/mame-i286)
target_compile_features(i286 PUBLIC cxx_std_20)

add_library(ega STATIC src/ega.cpp)
target_include_directories(ega PUBLIC src)
target_compile_features(ega PUBLIC cxx_std_20)

add_executable(boothush src/main.c src/mac128k.cpp src/pc286.cpp)
target_compile_features(boothush PRIVATE c_std_11 cxx_std_20)
target_link_libraries(boothush PRIVATE moira i286 ega)

if(MSVC)
    target_compile_options(moira PRIVATE /bigobj)
    target_compile_options(boothush PRIVATE /W4)
elseif(MINGW)
    target_compile_options(boothush PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(boothush PRIVATE -municode -static)
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME boothush COMMAND
        "${CMAKE_COMMAND}" -E env "BOOTHUSH_EXE=$<TARGET_FILE:boothush>"
        "BOOTHUSH_TEST_TMP=${CMAKE_CURRENT_BINARY_DIR}"
        "${Python3_EXECUTABLE}" -B -m unittest discover -v -s "${CMAKE_CURRENT_SOURCE_DIR}/tests"
    )
    set_tests_properties(boothush PROPERTIES TIMEOUT 120)

    add_executable(boothush_ega_test tests/test_ega.cpp)
    target_link_libraries(boothush_ega_test PRIVATE ega)
    if(MINGW)
        target_link_options(boothush_ega_test PRIVATE -static)
    endif()
    add_test(NAME ega COMMAND boothush_ega_test)
    set_tests_properties(ega PROPERTIES TIMEOUT 20)
endif()
