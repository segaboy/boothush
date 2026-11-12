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

add_library(boothush_devices STATIC
    src/ega.cpp
    src/iwm.cpp
    src/mac_keyboard.cpp
    src/mac_mouse.cpp
    src/mac_mouse_trace.cpp
    src/mac_rtc.cpp
    src/pc_at_devices.cpp
    src/pcat_floppy.cpp
    src/pcat_hard_disk.cpp
    src/pcat_keyboard.cpp
)
target_include_directories(boothush_devices PUBLIC src)
target_compile_features(boothush_devices PUBLIC cxx_std_20)

add_library(boothush_mac STATIC src/mac128k.cpp src/mac_live_window.cpp)
target_link_libraries(boothush_mac PUBLIC moira boothush_devices user32 gdi32)

add_executable(boothush src/main.c src/pc286.cpp)
target_compile_features(boothush PRIVATE c_std_11 cxx_std_20)
target_link_libraries(boothush PRIVATE boothush_mac i286)

if(MSVC)
    target_compile_options(moira PRIVATE /bigobj)
    target_compile_options(boothush_mac PRIVATE /W4)
    target_compile_options(boothush PRIVATE /W4)
elseif(MINGW)
    target_compile_options(boothush_devices PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(boothush_mac PRIVATE -Wall -Wextra -Wpedantic)
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

    foreach(device IN ITEMS ega mac_keyboard mac_mouse mac_rtc pc_at_devices pcat_floppy pcat_keyboard)
        set(test_target "boothush_${device}_test")
        add_executable("${test_target}" "tests/test_${device}.cpp")
        target_link_libraries("${test_target}" PRIVATE boothush_devices)
        if(MINGW)
            target_link_options("${test_target}" PRIVATE -static)
        endif()
        add_test(NAME "${device}" COMMAND "${test_target}")
        set_tests_properties("${device}" PROPERTIES TIMEOUT 20)
    endforeach()

    add_executable(boothush_mac_live_test tests/test_mac_live.cpp)
    target_link_libraries(boothush_mac_live_test PRIVATE boothush_mac bcrypt)
    if(MINGW)
        target_link_options(boothush_mac_live_test PRIVATE -static)
    endif()
    add_test(NAME mac_live COMMAND boothush_mac_live_test)
    set_tests_properties(mac_live PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)
endif()
