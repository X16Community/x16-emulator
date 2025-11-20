cmake_minimum_required(VERSION 3.15)

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
    set(SDL2_INCLUDE_DIRS "") # Emscripten handles the include path implicitly
    set(SDL2_LIBRARIES SDL2::SDL2)

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
find_package(PkgConfig)

# --- FluidSynth Auto-Detection ---
if(PKG_CONFIG_FOUND)
    pkg_check_modules(FLUIDSYNTH QUIET IMPORTED_TARGET fluidsynth)
endif()

if(EMSCRIPTEN)
    message(STATUS "FluidSynth not supported in the wasm build. MIDI support disabled.")
elseif(FLUIDSYNTH_FOUND)
    message(STATUS "FluidSynth found via PkgConfig. Enabling MIDI support.")
    add_compile_definitions(HAS_FLUIDSYNTH)
else()
    message(STATUS "FluidSynth not found. MIDI support disabled.")
endif()

# --- Trace Logging Auto-Detection ---
# Check if the specific ROM debug headers exist in the source tree
if(EMSCRIPTEN)
    message(STATUS "Trace logging disabled in wasm builds.")
    set(ENABLE_TRACE OFF)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/rom_lst.h" AND
       EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/rom_labels.h")
    set(ENABLE_TRACE ON)
    message(STATUS "Trace headers found (rom_lst.h, rom_labels.h). Enabling TRACE.")
else()
    set(ENABLE_TRACE OFF)
    message(STATUS "Trace headers not found. TRACE disabled.")
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
)

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
    ${SDL2_INCLUDE_DIRS}
    ${GIT_REV_HEADER_DIR}
)

if(ENABLE_TRACE)
    target_compile_definitions(x16emu PRIVATE TRACE)
endif()

# Linking
target_link_libraries(x16emu PRIVATE
    ${SDL2_LIBRARIES}
    ZLIB::ZLIB
)

if(FLUIDSYNTH_FOUND)
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
    ${SDL2_INCLUDE_DIRS}
    ${GIT_REV_HEADER_DIR}
)
target_link_libraries(makecart PRIVATE
    ZLIB::ZLIB
    ${SDL2_LIBRARIES}
)

if(EMSCRIPTEN)
    set_target_properties(makecart PROPERTIES SUFFIX ".html")
    target_link_options(makecart PRIVATE "SHELL:-s USE_ZLIB=1")
endif()
