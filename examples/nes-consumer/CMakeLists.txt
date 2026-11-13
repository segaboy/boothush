cmake_minimum_required(VERSION 3.21)
project(NESConsumer LANGUAGES C)
find_package(BootHushNES 0.1 CONFIG REQUIRED)
add_executable(nes_consumer main.c)
target_link_libraries(nes_consumer PRIVATE BootHush::nes)
if(WIN32)
    add_custom_command(TARGET nes_consumer POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:BootHush::nes>" "$<TARGET_FILE_DIR:nes_consumer>")
endif()
