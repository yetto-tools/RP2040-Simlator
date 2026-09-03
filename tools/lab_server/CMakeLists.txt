# rp2040-lab-server: the local HTTP/JSON backend for the browser-based
# "virtual lab" frontend (../../web). Links rp2040_core unchanged; the two
# vendored headers (httplib.h, json.hpp - see vendor/README.md) are third
# -party code not written to this project's own strict warning set, so this
# target intentionally does NOT link rp2040_project_options (unlike every
# other target in the tree) - rp2040_core itself stays fully warning-clean
# under -Werror, only this optional tool's compilation units are exempt.

find_program(ARM_NONE_EABI_GCC arm-none-eabi-gcc)
find_package(Threads REQUIRED)

# debug_session.{h,cpp} has no vendored-header dependency (unlike main.cpp,
# which pulls in httplib.h/json.hpp) and is exercised directly by
# tests/unit/test_debug_session.cpp, so it is its own small static library
# rather than being compiled straight into the server executable.
add_library(rp2040_lab_debug STATIC debug_session.cpp)
target_include_directories(rp2040_lab_debug PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rp2040_lab_debug PUBLIC rp2040_core Threads::Threads)
target_link_libraries(rp2040_lab_debug PRIVATE rp2040_project_options)

add_executable(rp2040-lab-server main.cpp compiler.cpp)
target_include_directories(rp2040-lab-server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rp2040-lab-server PRIVATE rp2040_lab_debug)
rp2040_copy_runtime_dlls(rp2040-lab-server)

if(WIN32)
    # httplib.h needs Windows 10 APIs (e.g. CreateFile2); MinGW's default
    # _WIN32_WINNT is older unless raised explicitly, before any Windows
    # header gets pulled in transitively.
    target_compile_definitions(rp2040-lab-server PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
endif()

if(ARM_NONE_EABI_GCC)
    target_compile_definitions(rp2040-lab-server PRIVATE
        RP2040LAB_ARM_GCC="${ARM_NONE_EABI_GCC}"
        RP2040LAB_LINKER_SCRIPT="${CMAKE_SOURCE_DIR}/tests/fixtures/firmware.ld")
    message(STATUS "rp2040-lab-server: /compile enabled (${ARM_NONE_EABI_GCC})")
else()
    message(STATUS "rp2040-lab-server: arm-none-eabi-gcc not found - /compile will report an error; /load still works")
endif()

# ----------------------------------------------------------------------------
# Optional pico-sdk compile mode (BACKLOG.md P10.3). Detected the same way
# the official pico-vscode extension lays its install out
# (~/.pico-sdk/{sdk,cmake,ninja}/<version>/...), which is what's present on
# this machine; a plain `$ENV{PICO_SDK_PATH}` (already set here) plus
# system cmake/ninja on PATH works too. Degrades gracefully - the freestanding
# /compile path above is unaffected either way.
# ----------------------------------------------------------------------------
if(WIN32)
    set(_pico_home "$ENV{USERPROFILE}/.pico-sdk")
else()
    set(_pico_home "$ENV{HOME}/.pico-sdk")
endif()

if(DEFINED ENV{PICO_SDK_PATH} AND EXISTS "$ENV{PICO_SDK_PATH}/pico_sdk_init.cmake")
    set(PICO_SDK_PATH_FOUND "$ENV{PICO_SDK_PATH}")
else()
    file(GLOB _pico_sdk_candidates "${_pico_home}/sdk/*/pico_sdk_init.cmake")
    if(_pico_sdk_candidates)
        list(GET _pico_sdk_candidates -1 _pico_sdk_init)
        get_filename_component(PICO_SDK_PATH_FOUND "${_pico_sdk_init}" DIRECTORY)
    endif()
endif()

file(GLOB _pico_cmake_candidates "${_pico_home}/cmake/*/bin/cmake.exe" "${_pico_home}/cmake/*/bin/cmake")
if(_pico_cmake_candidates)
    list(GET _pico_cmake_candidates -1 PICO_CMAKE_EXE)
else()
    set(PICO_CMAKE_EXE "${CMAKE_COMMAND}")  # this project's own cmake works fine too
endif()

file(GLOB _pico_ninja_candidates "${_pico_home}/ninja/*/ninja.exe" "${_pico_home}/ninja/*/ninja")
if(_pico_ninja_candidates)
    list(GET _pico_ninja_candidates -1 PICO_NINJA_EXE)
else()
    find_program(PICO_NINJA_EXE ninja)
endif()

if(PICO_SDK_PATH_FOUND AND PICO_CMAKE_EXE AND PICO_NINJA_EXE AND ARM_NONE_EABI_GCC)
    # Backslashes in a Windows path break inside a C string literal (each
    # "\U..."/"\s..." etc looks like an escape sequence to the preprocessor)
    # - normalize to forward slashes, which Windows accepts everywhere too.
    file(TO_CMAKE_PATH "${PICO_SDK_PATH_FOUND}" PICO_SDK_PATH_FOUND)
    file(TO_CMAKE_PATH "${PICO_CMAKE_EXE}" PICO_CMAKE_EXE)
    file(TO_CMAKE_PATH "${PICO_NINJA_EXE}" PICO_NINJA_EXE)
    get_filename_component(_toolchain_bin_dir "${ARM_NONE_EABI_GCC}" DIRECTORY)
    target_compile_definitions(rp2040-lab-server PRIVATE
        RP2040LAB_PICO_SDK_PATH="${PICO_SDK_PATH_FOUND}"
        RP2040LAB_PICO_CMAKE="${PICO_CMAKE_EXE}"
        RP2040LAB_PICO_NINJA="${PICO_NINJA_EXE}"
        RP2040LAB_PICO_TOOLCHAIN_DIR="${_toolchain_bin_dir}")
    message(STATUS "rp2040-lab-server: pico-sdk /compile mode enabled (${PICO_SDK_PATH_FOUND})")
else()
    message(STATUS "rp2040-lab-server: pico-sdk not found - pico_sdk compile mode will report an error; freestanding /compile still works")
endif()
