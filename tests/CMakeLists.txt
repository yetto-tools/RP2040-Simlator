# Test suite. doctest is vendored as a single header under tests/vendor and is
# NOT part of the core library (keeps the core dependency-free per CLAUDE.md).

add_library(doctest INTERFACE)
target_include_directories(doctest INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/vendor)

# Single translation unit that provides doctest's main(). Every test file links
# against this so we compile the framework only once.
add_library(doctest_main STATIC doctest_main.cpp)
target_link_libraries(doctest_main PUBLIC doctest)

# Helper: register one test executable from a source file.
function(rp2040_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE rp2040_core doctest doctest_main rp2040_project_options)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

rp2040_add_test(test_smoke unit/test_smoke.cpp)
rp2040_add_test(test_memory unit/test_memory.cpp)
rp2040_add_test(test_registers unit/test_registers.cpp)
rp2040_add_test(test_thumb_decode unit/test_thumb_decode.cpp)
rp2040_add_test(test_alu unit/test_alu.cpp)
rp2040_add_test(test_cpu_exec unit/test_cpu_exec.cpp)
rp2040_add_test(test_timing unit/test_timing.cpp)
rp2040_add_test(test_exceptions unit/test_exceptions.cpp)
rp2040_add_test(test_scs unit/test_scs.cpp)
rp2040_add_test(test_elf_loader unit/test_elf_loader.cpp)
rp2040_add_test(test_pio_decode unit/test_pio_decode.cpp)
rp2040_add_test(test_pio_sm unit/test_pio_sm.cpp)
rp2040_add_test(test_gpio unit/test_gpio.cpp)
rp2040_add_test(test_pio_block unit/test_pio_block.cpp)
rp2040_add_test(test_pio_registers unit/test_pio_registers.cpp)
rp2040_add_test(test_timer unit/test_timer.cpp)
rp2040_add_test(test_uart unit/test_uart.cpp)
rp2040_add_test(test_dma unit/test_dma.cpp)
rp2040_add_test(test_adc unit/test_adc.cpp)
rp2040_add_test(test_spi unit/test_spi.cpp)
rp2040_add_test(test_pwm unit/test_pwm.cpp)
rp2040_add_test(test_i2c unit/test_i2c.cpp)
rp2040_add_test(test_watchdog unit/test_watchdog.cpp)
rp2040_add_test(test_clocks unit/test_clocks.cpp)
rp2040_add_test(test_rtc unit/test_rtc.cpp)
rp2040_add_test(test_padsbank0 unit/test_padsbank0.cpp)

# ---------------------------------------------------------------------------
# Integration: build a real Cortex-M0+ firmware fixture with the ARM
# cross-toolchain (if present) and run it through the whole simulator.
# When arm-none-eabi-gcc is not found the test still builds and reports its
# cases as skipped.
# ---------------------------------------------------------------------------
rp2040_add_test(test_firmware integration/test_firmware.cpp)

find_program(ARM_NONE_EABI_GCC arm-none-eabi-gcc)
if(ARM_NONE_EABI_GCC)
    set(_fw_src  ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sum.c)
    set(_fw_ld   ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/firmware.ld)
    set(_fw_elf  ${CMAKE_CURRENT_BINARY_DIR}/sum.elf)
    add_custom_command(
        OUTPUT ${_fw_elf}
        COMMAND ${ARM_NONE_EABI_GCC}
                -mcpu=cortex-m0plus -mthumb -nostdlib -nostartfiles
                -ffreestanding -O2 -Wl,--build-id=none -Wl,--no-warn-rwx-segments
                -T ${_fw_ld} -o ${_fw_elf} ${_fw_src}
        DEPENDS ${_fw_src} ${_fw_ld}
        COMMENT "Building Cortex-M0+ firmware fixture (sum.elf)"
        VERBATIM)
    add_custom_target(firmware_fixture DEPENDS ${_fw_elf})
    add_dependencies(test_firmware firmware_fixture)
    target_compile_definitions(test_firmware PRIVATE "RP2040_FIRMWARE_SUM_ELF=\"${_fw_elf}\"")
    message(STATUS "Firmware integration test enabled (${ARM_NONE_EABI_GCC})")
else()
    message(STATUS "arm-none-eabi-gcc not found - firmware integration test will skip")
endif()
