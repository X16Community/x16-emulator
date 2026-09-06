cmake_minimum_required(VERSION 3.15)

string(ASCII 27 Esc)
set(ColorReset  "${Esc}[m")
set(ColorYellow "${Esc}[33m")
set(ColorGreen  "${Esc}[32m")
set(ColorRed    "${Esc}[31m")

project(x16emu LANGUAGES C CXX)

# --- Options (Replacements for Env Vars) ---
option(ENABLE_LTO "Enable Link Time Optimization" ON)

# --- Configuration & Standards ---
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(ENABLE_LTO)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
endif()

# Common compiler warnings/flags
add_compile_options(-Wall -Werror)
if(MSVC)
    add_compile_options(/WX) # Windows MSVC equivalent
endif()

set(GIT_REV_HEADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(GIT_REV_HEADER "${GIT_REV_HEADER_DIR}/git_rev.h")
file(MAKE_DIRECTORY ${GIT_REV_HEADER_DIR})

find_package(Git)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/GetGitRevision.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/GetGitRevision.cmake"
    @ONLY
)

# Create a custom target that runs EVERY build
add_custom_target(
    update_git_rev
    ALL # Ensures this runs by default
    COMMAND ${CMAKE_COMMAND}
        -DCMAKE_SOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DOUTPUT_FILE=${GIT_REV_HEADER}
        -P ${CMAKE_CURRENT_BINARY_DIR}/GetGitRevision.cmake
    BYPRODUCTS ${GIT_REV_HEADER}
    COMMENT "Checking Git revision..."
)

# --- Dependencies ---
if(EMSCRIPTEN)
    # Emscripten: SDL2 is a built-in port, not a package we search for.
    # We create a "Dummy" target that passes the compiler flags (-s USE_SDL=2)
    # to any target that links against it.
    add_library(SDL2::SDL2 INTERFACE IMPORTED)
    target_compile_options(SDL2::SDL2 INTERFACE "SHELL:-s USE_SDL=2")
    target_link_options(SDL2::SDL2 INTERFACE "SHELL:-s USE_SDL=2")

    set(SDL2_FOUND TRUE)

    # Emscripten ZLIB (Built-in port)
    add_library(ZLIB::ZLIB INTERFACE IMPORTED)
    target_compile_options(ZLIB::ZLIB INTERFACE "SHELL:-s USE_ZLIB=1")
    target_link_options(ZLIB::ZLIB INTERFACE "SHELL:-s USE_ZLIB=1")

    set(ZLIB_FOUND TRUE)
else()
    find_package(SDL2 REQUIRED)
    find_package(ZLIB REQUIRED)
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_package(PkgConfig QUIET)

# Fluidsynth

set(ENABLE_FLUIDSYNTH "AUTO" CACHE STRING "Enable FluidSynth MIDI support (ON/OFF/AUTO)")
set_property(CACHE ENABLE_FLUIDSYNTH PROPERTY STRINGS ON OFF AUTO)

set(USE_FLUIDSYNTH FALSE)

if(ENABLE_FLUIDSYNTH STREQUAL "OFF")
    message(STATUS "[x16] FluidSynth support explicitly disabled.")
elseif(EMSCRIPTEN)
    if (ENABLE_FLUIDSYNTH STREQUAL "ON")
        message(FATAL_ERROR "[x16] FluidSynth cannot be enabled for wasm builds.")
    elseif (ENABLE_FLUIDSYNTH STREQUAL "AUTO")
        message(STATUS "[x16] FluidSynth disabled for wasm builds.")
    endif()
else()
    if(NOT PKG_CONFIG_FOUND)
        if(ENABLE_FLUIDSYNTH STREQUAL "ON")
            message(FATAL_ERROR "FluidSynth requested (ON), but pkg-config is missing on this system.")
        else()
            message(WARNING "pkg-config missing. Skipping FluidSynth auto-detection.")
        endif()
    else()
        if(ENABLE_FLUIDSYNTH STREQUAL "ON")
            pkg_check_modules(FLUIDSYNTH REQUIRED IMPORTED_TARGET fluidsynth>=2.0)
            set(USE_FLUIDSYNTH TRUE)
            message(STATUS "[x16] FluidSynth found and enabled.")
        else() 
            pkg_check_modules(FLUIDSYNTH IMPORTED_TARGET fluidsynth>=2.0)
            if(FLUIDSYNTH_FOUND)
                set(USE_FLUIDSYNTH TRUE)
                message(STATUS "[x16] FluidSynth found and enabled (auto).")
            else()
                message(WARNING "[x16] FluidSynth not found. MIDI support disabled (auto).")
            endif()
        endif()
    endif()
endif()

# Trace Logging

set(ENABLE_TRACE "AUTO" CACHE STRING "Enable trace logging support (ON/OFF/AUTO)")
set_property(CACHE ENABLE_TRACE PROPERTY STRINGS ON OFF AUTO)

set(USE_TRACE FALSE)

if(ENABLE_TRACE STREQUAL "OFF")
    message(STATUS "[x16] Trace support explicitly disabled.")
elseif(EMSCRIPTEN)
    if (ENABLE_TRACE STREQUAL "ON")
        message(FATAL_ERROR "[x16] Trace cannot be enabled for wasm builds.")
    elseif (ENABLE_TRACE STREQUAL "AUTO")
        message(STATUS "[x16] Trace disabled for wasm builds.")
    endif()
else()
    if(ENABLE_TRACE STREQUAL "ON")
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/rom_lst.h" AND
        EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/rom_labels.h")
            set(USE_TRACE ON)
            message(STATUS "[x16] Trace enabled and headers found (rom_lst.h, rom_labels.h).")
        else()
            message(FATAL_ERROR "[x16] Trace enabled but headers (rom_lst.h, rom_labels.h) not found.")
        endif()
    else()
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/rom_lst.h" AND
        EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/rom_labels.h")
            set(USE_TRACE ON)
            message(STATUS "[x16] Trace enabled (auto) and headers found (rom_lst.h, rom_labels.h).")
        else()
            message(STATUS "[x16] Trace disabled (auto). Headers (rom_lst.h, rom_labels.h) not found.")
        endif()
    endif()
endif()

# --- Code Generation (Python Script) ---
# This replaces the 'cpu/tables.h' rule in the Makefile
set(TABLE_GEN_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/src/cpu/buildtables.py")
set(TABLE_OUTPUT_H "${CMAKE_CURRENT_SOURCE_DIR}/src/cpu/tables.h")
set(MNEMONIC_OUTPUT_H "${CMAKE_CURRENT_SOURCE_DIR}/src/cpu/mnemonics.h")

add_custom_command(
    OUTPUT ${TABLE_OUTPUT_H} ${MNEMONIC_OUTPUT_H}
    COMMAND "${Python3_EXECUTABLE}" "${TABLE_GEN_SCRIPT}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src/cpu"
    DEPENDS "${TABLE_GEN_SCRIPT}" "${CMAKE_CURRENT_SOURCE_DIR}/src/cpu/6502.opcodes"
    DEPENDS "${TABLE_GEN_SCRIPT}" "${CMAKE_CURRENT_SOURCE_DIR}/src/cpu/65c02.opcodes"
    DEPENDS "${TABLE_GEN_SCRIPT}" "${CMAKE_CURRENT_SOURCE_DIR}/src/cpu/65c816.opcodes"
    COMMENT "Generating CPU tables..."
)
# Create a target so the main executable can depend on it
add_custom_target(gen_cpu_tables DEPENDS ${TABLE_OUTPUT_H})

# --- Sources ---
set(X16_SOURCES
    src/cpu/fake6502.c
    src/memory.c
    src/disasm.c
    src/video.c
    src/i2c.c
    src/smc.c
    src/rtc.c
    src/via.c
    src/serial.c
    src/ieee.c
    src/vera_spi.c
    src/audio.c
    src/vera_pcm.c
    src/vera_psg.c
    src/sdcard.c
    src/main.c
    src/debugger.c
    src/javascript_interface.c
    src/joystick.c
    src/rendertext.c
    src/keyboard.c
    src/icon.c
    src/timing.c
    src/wav_recorder.c
    src/testbench.c
    src/files.c
    src/cartridge.c
    src/iso_8859_15.c
    src/ymglue.cpp
    src/midi.c
    src/extern/ymfm/src/ymfm_opm.cpp
    src/extern/serialuart/serialib/serialib.cpp
	src/extern/serialuart/serialuart_wrapper.cpp
    src/extern/serialuart/serialuartTL16C2550.cpp
    src/extern/serialuart/uart_backend.cpp
)

# --- optional in-process virtual modem: libximodem ---------------------------
# Enabled automatically when the ximodem source tree and a python3 are both
# present (POSIX and Windows/MSYS2 alike). Set USE_XIMODEM=OFF to force it off.
#
# The tree is normally the `extern/libximodem` git submodule -- clone with
# --recursive, or `git submodule update --init extern/libximodem`. A sibling
# `../libximodem` checkout, if present, is used instead, so you can work on both
# repos at once without disturbing the submodule pin. Override with -DXIMODEM_DIR.
option(USE_XIMODEM "Build the in-process libximodem virtual modem (-uart1 ximodem)" ON)
if(EXISTS "${CMAKE_SOURCE_DIR}/../libximodem/CMakeLists.txt")
    set(_ximodem_default "${CMAKE_SOURCE_DIR}/../libximodem")
else()
    set(_ximodem_default "${CMAKE_SOURCE_DIR}/extern/libximodem")
endif()
set(XIMODEM_DIR "${_ximodem_default}" CACHE PATH
    "Path to the ximodem shared-library source tree")
if(USE_XIMODEM AND EXISTS "${XIMODEM_DIR}/CMakeLists.txt")
    find_program(PYTHON3_FOR_XIMODEM NAMES python3 python)
    if(PYTHON3_FOR_XIMODEM)
        message(STATUS "libximodem: building from ${XIMODEM_DIR}")
        add_subdirectory("${XIMODEM_DIR}" "${CMAKE_BINARY_DIR}/ximodem")
        set(HAVE_XIMODEM ON)
    else()
        message(STATUS "libximodem: python3 not found -- '-uart1 ximodem' disabled")
    endif()
elseif(USE_XIMODEM)
    message(STATUS "libximodem: ${XIMODEM_DIR} missing -- run "
                   "'git submodule update --init extern/libximodem'. '-uart1 ximodem' disabled")
endif()

if(WIN32)
    list(APPEND X16_SOURCES src/video_win32.c)
endif()

# --- Main Executable Target ---
add_executable(x16emu ${X16_SOURCES})

# Ensure the Python script runs before compiling x16emu
add_dependencies(x16emu gen_cpu_tables)

# Add git revision header
add_dependencies(x16emu update_git_rev)

# Includes
target_include_directories(x16emu PRIVATE
    src
    src/extern/include
    src/extern/ymfm/src
    ${GIT_REV_HEADER_DIR}
)

if(USE_TRACE)
    target_compile_definitions(x16emu PRIVATE TRACE)
endif()

# Linking
target_link_libraries(x16emu PRIVATE
    SDL2::SDL2
    ZLIB::ZLIB
)

if(HAVE_XIMODEM)
    target_compile_definitions(x16emu PRIVATE HAVE_XIMODEM)
    target_include_directories(x16emu PRIVATE "${XIMODEM_DIR}/include")
    target_link_libraries(x16emu PRIVATE ximodem)

    # Do NOT bake the absolute build-tree path into x16emu's RPATH (CMake's
    # default). Instead resolve libximodem the same way every other shared lib
    # is resolved: relative to the executable, then the system loader path.
    #   - drop libximodem.so next to x16emu  -> found via $ORIGIN / @loader_path
    #   - or install it to a system lib dir  -> found via the default search
    # So the binary is relocatable and works from anywhere it is copied.
    set_target_properties(ximodem PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")   # sits beside x16emu
    if(NOT WIN32)
        if(APPLE)
            set(_xi_origin_rpath "@loader_path")
        else()
            set(_xi_origin_rpath "$ORIGIN")
        endif()
        set_target_properties(x16emu PROPERTIES
            SKIP_BUILD_RPATH ON
            BUILD_WITH_INSTALL_RPATH ON
            INSTALL_RPATH "${_xi_origin_rpath}")
    endif()

    message(STATUS
        "libximodem: x16emu resolves it relatively (no absolute RPATH). Keep "
        "'libximodem${CMAKE_SHARED_LIBRARY_SUFFIX}' next to the x16emu binary, "
        "or install it to a system library directory.")

    # Every build reprints the co-location rule -- the configure-time message
    # above scrolls past long before the binary is done.
    if(NOT WIN32)
        add_custom_command(TARGET x16emu POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo
                "  note: x16emu loads libximodem${CMAKE_SHARED_LIBRARY_SUFFIX} via \$ORIGIN --"
            COMMAND ${CMAKE_COMMAND} -E echo
                "  when you copy x16emu elsewhere, copy build/libximodem${CMAKE_SHARED_LIBRARY_SUFFIX} into the same directory."
            VERBATIM)
    endif()

    install(TARGETS x16emu ximodem
            RUNTIME DESTINATION bin
            LIBRARY DESTINATION lib)
endif()

if(TARGET SDL2::SDL2main)
    target_link_libraries(x16emu PRIVATE SDL2::SDL2main)
endif()

if(USE_FLUIDSYNTH)
    add_compile_definitions(HAS_FLUIDSYNTH)
    target_link_libraries(x16emu PRIVATE PkgConfig::FLUIDSYNTH)
endif()

# Windows Specifics
if(WIN32)
    # MinGW Linker flags
    if(MINGW)
        target_link_options(x16emu PRIVATE
            -static-libstdc++
            -static-libgcc
            -Wl,--subsystem,console
        )
    endif()
    target_link_libraries(x16emu PRIVATE dwmapi)
endif()

# macOS Specifics
if(APPLE)

    find_library(COREAUDIO CoreAudio REQUIRED)
    find_library(AUDIOTOOLBOX AudioToolbox REQUIRED)
    find_library(FORCEFEEDBACK ForceFeedback REQUIRED)
    find_library(COREVIDEO CoreVideo REQUIRED)
    find_library(COCOA Cocoa REQUIRED)
    find_library(CARBON Carbon REQUIRED)
    find_library(IOKIT IOKit REQUIRED)
    find_library(ICONV iconv REQUIRED)
    find_library(QUARTZCORE QuartzCore REQUIRED)
    find_library(METAL Metal REQUIRED)
    find_library(COREHAPTICS CoreHaptics REQUIRED)
    find_library(GAMECONTROLLER GameController REQUIRED)

    target_link_libraries(x16emu PRIVATE
        ${COREAUDIO} ${AUDIOTOOLBOX} ${FORCEFEEDBACK}
        ${COREVIDEO} ${COCOA} ${CARBON} ${IOKIT} ${ICONV}
        ${QUARTZCORE} ${METAL} ${COREHAPTICS} ${GAMECONTROLLER}
    )
endif()

# Linux/Default Specifics
if(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
    target_link_libraries(x16emu PRIVATE dl pthread m)
endif()

# Emscripten
if(EMSCRIPTEN)
    set_target_properties(x16emu PROPERTIES SUFFIX ".html")
    target_link_options(x16emu PRIVATE
        --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/webassembly/x16emu-template.html
        --preload-file ${CMAKE_CURRENT_SOURCE_DIR}/build/rom.bin
        "SHELL:-s TOTAL_MEMORY=32MB"
        "SHELL:-s ASSERTIONS=1"
        "SHELL:-s DISABLE_DEPRECATED_FIND_EVENT_TARGET_BEHAVIOR=1"
        "SHELL:-s EXPORTED_FUNCTIONS=['_j2c_reset','_j2c_paste','_j2c_start_audio','_main']"
        "SHELL:-s EXPORTED_RUNTIME_METHODS=['ccall','cwrap']"
        "SHELL:-s USE_ZLIB=1"
        "SHELL:-s EXIT_RUNTIME=1"
    )
endif()

# --- Secondary Target: Makecart ---
add_executable(makecart
    src/makecart.c
    src/files.c
    src/cartridge.c
    src/makecart_javascript_interface.c
)

add_dependencies(makecart update_git_rev)

# Makecart dependencies
target_include_directories(makecart PRIVATE
    src
    src/extern/include
    ${GIT_REV_HEADER_DIR}
)
target_link_libraries(makecart PRIVATE
    ZLIB::ZLIB
    SDL2::SDL2
)

if(TARGET SDL2::SDL2main)
    target_link_libraries(makecart PRIVATE SDL2::SDL2main)
endif()

if(EMSCRIPTEN)
    set_target_properties(makecart PROPERTIES SUFFIX ".html")
    target_link_options(makecart PRIVATE "SHELL:-s USE_ZLIB=1")
endif()

# Build summary

if(ENABLE_FLUIDSYNTH STREQUAL "AUTO" AND NOT USE_FLUIDSYNTH)
    set(FLUID_EN_MSG "${ColorYellow}DISABLED (Missing Dependency)${ColorReset}")
elseif(USE_FLUIDSYNTH)
    set(FLUID_EN_MSG "${ColorGreen}ENABLED${ColorReset}")
else()
    set(FLUID_EN_MSG "DISABLED (Explicit)")
endif()

if(ENABLE_TRACE STREQUAL "AUTO" AND NOT USE_TRACE)
    set(TRACE_EN_MSG "${ColorYellow}DISABLED (Missing ROM Headers)${ColorReset}")
elseif(USE_TRACE)
    set(TRACE_EN_MSG "${ColorGreen}ENABLED${ColorReset}")
else()
    set(TRACE_EN_MSG "DISABLED (Explicit)")
endif()

message(STATUS "\n")
message(STATUS "=========================================================================")
message(STATUS "   x16-emulator Build Options Summary")
message(STATUS "=========================================================================")
message(STATUS "   Fluidsynth Support : ${FLUID_EN_MSG}")
message(STATUS "   Trace Support      : ${TRACE_EN_MSG}")
message(STATUS "=========================================================================\n")
