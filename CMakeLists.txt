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
# Core simulator library (self-contained, no external dependencies)
# ----------------------------------------------------------------------------
add_library(rp2040_core STATIC
    src/simulator.cpp
    src/core/bus.cpp
    src/core/memory.cpp
    src/core/registers.cpp
    src/core/thumb_decode.cpp
    src/core/alu.cpp
    src/core/cpu.cpp
    src/core/timing.cpp
    src/core/scs.cpp
    src/loaders/elf_loader.cpp
    src/pio/pio_decode.cpp
    src/pio/state_machine.cpp
    src/pio/pio_block.cpp
    src/pio/pio_registers.cpp
    src/peripherals/gpio.cpp
    src/peripherals/sio.cpp
    src/peripherals/iobank0.cpp
    src/peripherals/timer.cpp
    src/peripherals/uart.cpp
    src/peripherals/adc.cpp
    src/peripherals/dma.cpp
    src/peripherals/i2c.cpp
    src/peripherals/resets.cpp
    src/peripherals/watchdog.cpp
    src/peripherals/pwm.cpp
    src/peripherals/spi.cpp
)
target_include_directories(rp2040_core PUBLIC include src)
target_link_libraries(rp2040_core PRIVATE rp2040_project_options)

# ----------------------------------------------------------------------------
# CLI front-end
# ----------------------------------------------------------------------------
add_executable(rp2040-sim src/main.cpp)
target_link_libraries(rp2040-sim PRIVATE rp2040_core rp2040_project_options)

# ----------------------------------------------------------------------------
# Tests
# ----------------------------------------------------------------------------
if(RP2040_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
