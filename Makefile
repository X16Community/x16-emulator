##################################################################################################
#
# COMMANDER X16 EMULATOR MAKEFILE
#
##################################################################################################

BUILD_DIR = build
WASM_BUILD_DIR = build-wasm

.PHONY: all clean distclean wasm wasm-clean

all:
	@cmake -E echo "Building with CMake"
	cmake -E make_directory $(BUILD_DIR)
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD_DIR) $(ARGS)

	@cmake -E echo "x16emu and makecart executables can be found in ./$(BUILD_DIR)"
clean:
	-cmake --build $(BUILD_DIR) --target clean

distclean:
	cmake -E remove_directory $(BUILD_DIR)

# CMake needs this toolchain file to know it's cross-compiling for Emscripten.
# Without it, it would use the host compiler instead of emcc.
# The CI workflow uses emcmake (which passes this file automatically), but
# emcmake is not always available locally, so we locate the file ourselves.
EMSCRIPTEN_TOOLCHAIN ?= $(shell \
	emcc=$$(command -v emcc 2>/dev/null) && \
	dir=$$(dirname $$(readlink -f "$$emcc")) && \
	echo "$$dir/cmake/Modules/Platform/Emscripten.cmake")

wasm:
	@cmake -E echo "Building WASM target with Emscripten"
	cmake -E make_directory $(WASM_BUILD_DIR)
	cp -n rom.bin $(WASM_BUILD_DIR)/ 2>/dev/null || true
	cp -n build/rom.bin $(WASM_BUILD_DIR)/ 2>/dev/null || true
	cmake -S . -B $(WASM_BUILD_DIR) -DCMAKE_TOOLCHAIN_FILE=$(EMSCRIPTEN_TOOLCHAIN) -DENABLE_FLUIDSYNTH=OFF -DENABLE_TRACE=OFF
	cmake --build $(WASM_BUILD_DIR) $(ARGS)
	@cmake -E echo "Packaging WASM artifacts into $(WASM_BUILD_DIR)/emu_binaries"
	cmake -E make_directory $(WASM_BUILD_DIR)/emu_binaries/webassembly
	cp $(WASM_BUILD_DIR)/x16emu.data $(WASM_BUILD_DIR)/x16emu.html $(WASM_BUILD_DIR)/x16emu.js $(WASM_BUILD_DIR)/x16emu.wasm $(WASM_BUILD_DIR)/emu_binaries/
	cp webassembly/styles.css webassembly/main.js webassembly/jszip.min.js $(WASM_BUILD_DIR)/emu_binaries/webassembly/
	@cmake -E echo "WASM artifacts can be found in ./$(WASM_BUILD_DIR)/emu_binaries"

wasm-clean:
	-cmake --build $(WASM_BUILD_DIR) --target clean 2>/dev/null
