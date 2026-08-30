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
