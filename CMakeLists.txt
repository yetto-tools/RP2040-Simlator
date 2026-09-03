cmake_minimum_required(VERSION 3.16)

project(rp2040_simulator
    VERSION 0.1.0
    DESCRIPTION "Cycle-accurate RP2040 microcontroller simulator"
    LANGUAGES CXX)

# ----------------------------------------------------------------------------
# Global settings
# ----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "" FORCE)
endif()

option(RP2040_BUILD_TESTS "Build the unit/integration test suite" ON)
option(RP2040_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

# Warning flags shared by every target we own (not third-party code).
add_library(rp2040_project_options INTERFACE)
if(MSVC)
    target_compile_options(rp2040_project_options INTERFACE /W4 /permissive-)
    if(RP2040_WARNINGS_AS_ERRORS)
        target_compile_options(rp2040_project_options INTERFACE /WX)
    endif()
else()
    target_compile_options(rp2040_project_options INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)
    if(RP2040_WARNINGS_AS_ERRORS)
        target_compile_options(rp2040_project_options INTERFACE -Werror)
    endif()
endif()

# ----------------------------------------------------------------------------
# MinGW runtime DLLs: copy alongside every executable's output so it runs
# standalone. Without this, the .exe depends on whichever MinGW installation's
# libstdc++-6.dll / libgcc_s_seh-1.dll / libwinpthread-1.dll happens to be
# first on PATH at runtime - multiple MinGW installs (e.g. Git for Windows'
# bundled one) commonly shadow each other there and produce
# STATUS_ENTRY_POINT_NOT_FOUND crashes. Windows' DLL search checks the exe's
# own directory before PATH, so a local copy always wins.
# ----------------------------------------------------------------------------
if(MINGW)
    get_filename_component(RP2040_MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(RP2040_MINGW_RUNTIME_DLLS "")
    foreach(_dll libstdc++-6.dll libgcc_s_seh-1.dll libgcc_s_dw2-1.dll libwinpthread-1.dll)
        if(EXISTS "${RP2040_MINGW_BIN_DIR}/${_dll}")
            list(APPEND RP2040_MINGW_RUNTIME_DLLS "${RP2040_MINGW_BIN_DIR}/${_dll}")
        endif()
    endforeach()
endif()

# Copy the MinGW runtime DLLs (if any) next to a target's output binary.
function(rp2040_copy_runtime_dlls target)
    if(MINGW AND RP2040_MINGW_RUNTIME_DLLS)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${RP2040_MINGW_RUNTIME_DLLS} "$<TARGET_FILE_DIR:${target}>"
            COMMENT "Copying MinGW runtime DLLs next to ${target}"
            VERBATIM)
    endif()
endfunction()

# ----------------------------------------------------------------------------
# Core simulator library (self-contained, no external dependencies)
# ----------------------------------------------------------------------------
add_library(rp2040_core STATIC
    src/simulator.cpp
    src/debuggers/gdb_stub.cpp
    src/core/bus.cpp
    src/core/memory.cpp
    src/core/registers.cpp
    src/core/thumb_decode.cpp
    src/core/alu.cpp
    src/core/cpu.cpp
    src/core/timing.cpp
    src/core/scs.cpp
    src/loaders/elf_loader.cpp
    src/loaders/uf2_loader.cpp
    src/pio/pio_decode.cpp
    src/pio/pio_assembler.cpp
    src/pio/pio_disasm.cpp
    src/debuggers/pio_debugger.cpp
    src/debuggers/profiler.cpp
    src/pio/state_machine.cpp
    src/pio/pio_block.cpp
    src/pio/pio_registers.cpp
    src/peripherals/gpio.cpp
    src/peripherals/sio.cpp
    src/peripherals/iobank0.cpp
    src/peripherals/timer.cpp
    src/peripherals/uart.cpp
    src/peripherals/adc.cpp
    src/peripherals/clocks.cpp
    src/peripherals/clock_tree.cpp
    src/peripherals/padsbank0.cpp
    src/peripherals/rtc.cpp
    src/peripherals/dma.cpp
    src/peripherals/i2c.cpp
    src/peripherals/resets.cpp
    src/peripherals/sysinfo.cpp
    src/peripherals/watchdog.cpp
    src/peripherals/pwm.cpp
    src/peripherals/spi.cpp
    src/peripherals/usb.cpp
    src/peripherals/st7789.cpp
    src/peripherals/ssd1306.cpp
)
target_include_directories(rp2040_core PUBLIC include src)
target_link_libraries(rp2040_core PRIVATE rp2040_project_options)
if(WIN32)
    target_link_libraries(rp2040_core PUBLIC ws2_32)
endif()

# ----------------------------------------------------------------------------
# CLI front-end
# ----------------------------------------------------------------------------
add_executable(rp2040-sim src/main.cpp)
target_link_libraries(rp2040-sim PRIVATE rp2040_core rp2040_project_options)
rp2040_copy_runtime_dlls(rp2040-sim)

# ----------------------------------------------------------------------------
# Local web "virtual lab" backend (browser frontend lives in web/). Added
# before tests/ so test_debug_session.cpp can link the rp2040_lab_debug
# target it defines.
# ----------------------------------------------------------------------------
option(RP2040_BUILD_LAB_SERVER "Build the local web lab server (rp2040-lab-server)" ON)
if(RP2040_BUILD_LAB_SERVER)
    add_subdirectory(tools/lab_server)
endif()

# ----------------------------------------------------------------------------
# Tests
# ----------------------------------------------------------------------------
if(RP2040_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
