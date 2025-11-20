##################################################################################################
#
# COMMANDER X16 EMULATOR MAKEFILE
#
##################################################################################################

.PHONY: all clean

all:
	@echo "Building with CMake"
	[[ -e build ]] || mkdir build
	cd build && cmake .. && cmake --build .

	@echo "x16emu and makecart executables can be found in ./build"
clean:
	rm -rf build
