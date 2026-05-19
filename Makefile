##################################################################################################
#
# COMMANDER X16 EMULATOR MAKEFILE
#
##################################################################################################

BUILD_DIR = build

.PHONY: all clean distclean

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
