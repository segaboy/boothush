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

add_library(boothush_x64_media STATIC src/x64_media.cpp src/x64_pe_loader.cpp src/x64_soft_probe.cpp)
target_include_directories(boothush_x64_media PUBLIC src)
target_compile_features(boothush_x64_media PUBLIC cxx_std_20)
target_link_libraries(boothush_x64_media PUBLIC bcrypt)

add_library(boothush_x64_firmware_model STATIC src/x64_boot_services.cpp src/x64_efi_memory.cpp)
target_include_directories(boothush_x64_firmware_model PUBLIC src)
target_compile_features(boothush_x64_firmware_model PUBLIC cxx_std_20)

add_library(boothush_nes_core STATIC src/nes.cpp)
target_include_directories(boothush_nes_core PUBLIC src)
target_compile_features(boothush_nes_core PUBLIC cxx_std_20)
add_library(boothush_nes SHARED src/nes_api.cpp)
add_library(BootHush::nes ALIAS boothush_nes)
set_target_properties(boothush_nes PROPERTIES EXPORT_NAME nes PREFIX "")
target_compile_definitions(boothush_nes PRIVATE BOOTHUSH_NES_BUILD)
target_include_directories(boothush_nes PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_link_libraries(boothush_nes PRIVATE boothush_nes_core)
if(MINGW)
    target_link_options(boothush_nes PRIVATE -static -static-libgcc -static-libstdc++)
endif()
include(CMakePackageConfigHelpers)
configure_package_config_file(cmake/BootHushNESConfig.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/BootHushNESConfig.cmake" INSTALL_DESTINATION lib/cmake/BootHushNES)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/BootHushNESConfigVersion.cmake" VERSION 0.1.1 COMPATIBILITY SameMinorVersion)
install(TARGETS boothush_nes EXPORT BootHushNESTargets RUNTIME DESTINATION bin LIBRARY DESTINATION lib ARCHIVE DESTINATION lib)
install(FILES include/boothush/nes.h DESTINATION include/boothush)
install(EXPORT BootHushNESTargets NAMESPACE BootHush:: DESTINATION lib/cmake/BootHushNES)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/BootHushNESConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/BootHushNESConfigVersion.cmake" DESTINATION lib/cmake/BootHushNES)

add_executable(boothush src/main.c src/pc286.cpp src/video_capture.c src/media_convert.c)
target_compile_features(boothush PRIVATE c_std_11 cxx_std_20)
target_link_libraries(boothush PRIVATE boothush_mac i286 boothush_x64_media boothush_nes_core)

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
    # Independent, test-only reference core; never selected by the public API or CLI.
    add_library(agnes_reference STATIC third_party/agnes/agnes.c)
    target_include_directories(agnes_reference SYSTEM PUBLIC third_party/agnes)
    add_executable(boothush_nes_reference_test tests/test_nes_reference.c)
    target_link_libraries(boothush_nes_reference_test PRIVATE agnes_reference BootHush::nes)
    if(MINGW)
        target_link_options(boothush_nes_reference_test PRIVATE -static)
    endif()
    add_test(NAME nes_reference COMMAND boothush_nes_reference_test)
    set_tests_properties(nes_reference PROPERTIES TIMEOUT 30)
    add_executable(boothush_mac_live_cli_test src/main.c src/pc286.cpp src/video_capture.c src/media_convert.c tests/mac_live_cli_window.cpp)
    target_compile_features(boothush_mac_live_cli_test PRIVATE c_std_11 cxx_std_20)
    target_compile_definitions(boothush_mac_live_cli_test PRIVATE
        "$<$<COMPILE_LANGUAGE:C>:mac_live_window_create=boothush_test_window_create>")
    target_link_libraries(boothush_mac_live_cli_test PRIVATE boothush_mac i286 boothush_x64_media boothush_nes_core)
    if(MINGW)
        target_link_options(boothush_mac_live_cli_test PRIVATE -municode -static)
    endif()
    add_test(NAME boothush COMMAND
        "${CMAKE_COMMAND}" -E env "BOOTHUSH_EXE=$<TARGET_FILE:boothush>"
        "BOOTHUSH_LIVE_CLI_TEST_EXE=$<TARGET_FILE:boothush_mac_live_cli_test>"
        "BOOTHUSH_TEST_TMP=${CMAKE_CURRENT_BINARY_DIR}"
        "${Python3_EXECUTABLE}" -B -m unittest discover -v -s "${CMAKE_CURRENT_SOURCE_DIR}/tests"
    )
    set_tests_properties(boothush PROPERTIES TIMEOUT 120)

    add_executable(boothush_x64_boot_services_test tests/test_x64_boot_services.cpp)
    target_link_libraries(boothush_x64_boot_services_test PRIVATE boothush_x64_firmware_model)
    if(MINGW)
        target_link_options(boothush_x64_boot_services_test PRIVATE -static)
    endif()
    add_test(NAME x64_boot_services COMMAND boothush_x64_boot_services_test)
    set_tests_properties(x64_boot_services PROPERTIES TIMEOUT 20)

    add_executable(boothush_x64_efi_memory_test tests/test_x64_efi_memory.cpp)
    target_link_libraries(boothush_x64_efi_memory_test PRIVATE boothush_x64_firmware_model boothush_x64_media)
    if(MINGW)
        target_link_options(boothush_x64_efi_memory_test PRIVATE -static)
    endif()
    add_test(NAME x64_efi_memory COMMAND boothush_x64_efi_memory_test)
    set_tests_properties(x64_efi_memory PROPERTIES TIMEOUT 20)

    add_executable(boothush_x64_pe_loader_test tests/test_x64_pe_loader.cpp)
    target_link_libraries(boothush_x64_pe_loader_test PRIVATE boothush_x64_media)
    if(MINGW)
        target_link_options(boothush_x64_pe_loader_test PRIVATE -static)
    endif()
    add_test(NAME x64_pe_loader COMMAND boothush_x64_pe_loader_test)
    set_tests_properties(x64_pe_loader PROPERTIES TIMEOUT 20)

    add_executable(boothush_x64_soft_probe_test tests/test_x64_soft_probe.cpp)
    target_link_libraries(boothush_x64_soft_probe_test PRIVATE boothush_x64_media)
    if(MINGW)
        target_link_options(boothush_x64_soft_probe_test PRIVATE -static)
    endif()
    add_test(NAME x64_soft_probe COMMAND boothush_x64_soft_probe_test)
    set_tests_properties(x64_soft_probe PROPERTIES TIMEOUT 20)

    add_executable(boothush_nes_cpu_test tests/test_nes_cpu.cpp)
    if(MINGW)
        target_link_options(boothush_nes_cpu_test PRIVATE -static)
    endif()
    add_test(NAME nes_cpu COMMAND boothush_nes_cpu_test)
    set_tests_properties(nes_cpu PROPERTIES TIMEOUT 20)

    add_executable(boothush_nes_api_test tests/test_nes_public_api.c)
    target_link_libraries(boothush_nes_api_test PRIVATE BootHush::nes)
    if(MINGW)
        target_link_options(boothush_nes_api_test PRIVATE -static)
    endif()
    add_test(NAME nes_public_api COMMAND boothush_nes_api_test)
    set_tests_properties(nes_public_api PROPERTIES TIMEOUT 30)

    foreach(device IN ITEMS ega mac_keyboard mac_mouse mac_rtc pc_at_devices pcat_floppy pcat_keyboard pcat_mouse)
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

    add_executable(boothush_live_keyboard_test tests/test_live_keyboard.cpp)
    add_executable(boothush_mac_live_relative_test tests/test_mac_live_relative.cpp)
    target_link_libraries(boothush_mac_live_relative_test PRIVATE boothush_mac)
    target_link_libraries(boothush_live_keyboard_test PRIVATE boothush_mac)
    add_executable(boothush_live_keyboard_cli_test src/main.c src/pc286.cpp src/video_capture.c src/media_convert.c tests/live_keyboard_cli_window.cpp)
    target_compile_features(boothush_live_keyboard_cli_test PRIVATE c_std_11 cxx_std_20)
    target_compile_definitions(boothush_live_keyboard_cli_test PRIVATE
        "$<$<COMPILE_LANGUAGE:C>:mac_live_window_create=boothush_key_test_mac_create>"
        "$<$<COMPILE_LANGUAGE:C>:bh_live_window_create=boothush_key_test_create>")
    target_link_libraries(boothush_live_keyboard_cli_test PRIVATE boothush_mac i286 boothush_x64_media boothush_nes_core)
    if(MINGW)
        target_link_options(boothush_live_keyboard_test PRIVATE -static)
        target_link_options(boothush_mac_live_relative_test PRIVATE -static)
        target_link_options(boothush_live_keyboard_cli_test PRIVATE -municode -static)
    endif()
    add_test(NAME live_keyboard COMMAND boothush_live_keyboard_test)
    add_test(NAME mac_live_relative COMMAND boothush_mac_live_relative_test)
    add_test(NAME live_keyboard_cli COMMAND "${CMAKE_COMMAND}" -E env
        "BOOTHUSH_EXE=$<TARGET_FILE:boothush>"
        "BOOTHUSH_LIVE_KEY_TEST_EXE=$<TARGET_FILE:boothush_live_keyboard_cli_test>"
        "BOOTHUSH_TEST_TMP=${CMAKE_CURRENT_BINARY_DIR}"
        "${Python3_EXECUTABLE}" -B -m unittest discover -v -s "${CMAKE_CURRENT_SOURCE_DIR}/tests" -p test_live_keyboard_cli.py)
    add_test(NAME human_session COMMAND "${CMAKE_COMMAND}" -E env
        "BOOTHUSH_EXE=$<TARGET_FILE:boothush>"
        "BOOTHUSH_LIVE_KEY_TEST_EXE=$<TARGET_FILE:boothush_live_keyboard_cli_test>"
        "BOOTHUSH_TEST_TMP=${CMAKE_CURRENT_BINARY_DIR}"
        "${Python3_EXECUTABLE}" -B -m unittest discover -v -s "${CMAKE_CURRENT_SOURCE_DIR}/tests" -p test_human_session.py)
    set_tests_properties(live_keyboard mac_live_relative live_keyboard_cli human_session PROPERTIES TIMEOUT 30)
endif()
