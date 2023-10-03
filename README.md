<p align="center">
  <img src="./.gh/logo.png" />
</p>

[![Build Status](https://github.com/x16community/x16-emulator/actions/workflows/build.yml/badge.svg)](https://github.com/x16community/x16-emulator/actions/workflows/build.yml)
[![Release](https://img.shields.io/github/v/release/x16community/x16-emulator)](https://github.com/x16community/x16-emulator/releases)
[![License: BSD-Clause](https://img.shields.io/github/license/x16community/x16-emulator)](./LICENSE)
[![Contributors](https://img.shields.io/github/contributors/x16community/x16-emulator.svg)](https://github.com/x16community/x16-emulator/graphs/contributors)

This is an emulator for the Commander X16 computer system. It only depends on SDL2 and should compile on all modern operating systems.

Features
--------

* CPU: Full 65C02 instruction set
* VERA
	* Mostly cycle exact emulation
	* Supports almost all features:
		* composer
		* two layers
		* sprites
		* VSYNC, raster, sprite IRQ
* Sound
	* PCM
	* PSG
	* YM2151
* Real-Time-Clock
* NVRAM
* System Management Controller
* SD card: reading and writing (image file)
* VIA
	* ROM/RAM banking
	* keyboard
	* mouse
	* gamepads

Binaries & Compiling
--------------------

Binary releases for macOS, Windows and Linux are available on the [releases page][releases].

The emulator itself is dependent only on SDL2. However, to run the emulated system you will also need a compatible `rom.bin` ROM image. This will be
loaded from the directory containing the emulator binary, or you can use the `-rom .../path/to/rom.bin` option.

> __WARNING:__ Older versions of the ROM might not work in newer versions of the emulator, and vice versa.

You can build a ROM image yourself using the [build instructions][x16rom-build] in the [x16-rom] repo. The `rom.bin` included in the [_latest_ release][releases] of the emulator may also work with the HEAD of this repo, but this is not guaranteed.

### macOS Build

Install SDL2 using `brew install sdl2`.

### Linux Build

The SDL2 development package is available as a distribution package with most major versions of Linux:
- Red Hat: `yum install SDL2-devel`
- Debian: `apt-get install libsdl2-dev`

Type `make` to build the source. The output will be `x16emu` in the current directory. Remember you will also need a `rom.bin` as described above.

### WebAssembly Build

Steps for compiling WebAssembly/HTML5 can be found [here][webassembly].

### Windows Build

Currently macOS/Linux/MSYS2 is needed to build for Windows. Install mingw-w64 toolchain and mingw32 version of SDL.
Type the following command to build the source:
```
CROSS_COMPILE_WINDOWS=1 MINGW32=/usr/x86_64-w64-mingw32 WIN_SDL2=/usr/x86_64-w64-mingw32 make
```
Paths to those libraries can be changed to your installation directory if they aren't located there.

The output will be `x16emu.exe` in the current directory. Remember you will also need a `rom.bin` as described above and `SDL2.dll` in SDL2's binary folder.


Starting
--------

You can start `x16emu`/`x16emu.exe` either by double-clicking it, or from the command line. The latter allows you to specify additional arguments.

* When starting `x16emu` without arguments, it will pick up the system ROM (`rom.bin`) from the executable's directory.
* The system ROM filename/path can be overridden with the `-rom` command line argument.
* `-prg` lets you specify a `.prg` file that gets loaded after start. It is fetched from the host filesystem, even if an SD card is attached!
* `-bas` lets you specify a BASIC program in ASCII format that automatically typed in (and tokenized).
* `-run` executes the application specified through `-prg` or `-bas` using `RUN`.
* `-scale` scales video output to an integer multiple of 640x480
* `-rtc` causes the real-time-clock set to the system's time and date.
* `-echo [{iso|raw}]` causes all KERNAL/BASIC output to be printed to the host's terminal. Enable this and use the BASIC command "LIST" to convert a BASIC program to ASCII (detokenize).
* `-rom <rom.bin>` Override KERNAL/BASIC/* ROM file.
* `-ram <ramsize>` specifies banked RAM size in KB (8, 16, 32, ..., 2048). The default is 512.
* `-cart <crtfile.crt>` loads a cartridge file. This requires a specially formatted cartridge file, as specified in the documentation.
* `-cartbin <romfile.bin>` loads a raw cartridge file. This will be loaded starting at ROM bank 32. All cart banks will be flagged as RAM.
* `-joy1` , `-joy2`, `-joy3`, `-joy4` enables binding a gamepad to that SNES controller port
* `-nvram` lets you specify a 64 byte file for the system's non-volatile RAM. If it does not exist, it will be created once the NVRAM is modified.
* `-keymap` tells the KERNAL to switch to a specific keyboard layout. Use it without an argument to view the supported layouts.
* `-sdcard` lets you specify an SD card image (partition table + FAT32). Without this option, drive 8 will interface to the current directory on the host.
* `-fsroot <dir>` specifies a file system root for the HostFS interface. This lets you save and load files without an SD card image. (As of R42, this is the preferred method.)
* `-serial` makes accesses to the host filesystem go through the Serial Bus [experimental].
* `-nohostieee` disables IEEE API interception to access the host fs.
* `-warp` causes the emulator to run as fast as possible, possibly faster than a real X16.
* `-gif <filename>[,wait]` to record the screen into a GIF. See below for more info.
* `-wav <filename>[{,wait|,auto}]` to record audio into a WAV. See below for more info.
* `-quality` change image scaling algorithm quality
	* `nearest`: nearest pixel sampling
	* `linear`: linear filtering
	* `best`: (default) anisotropic filtering
* `-log` enables one or more types of logging (e.g. `-log KS`):
	* `K`: keyboard (key-up and key-down events)
	* `S`: speed (CPU load, frame misses)
	* `V`: video I/O reads and writes
* `-debug` enables the debugger.
* `-dump` configure system dump (e.g. `-dump CB`):
	* `C`: CPU registers (7 B: A,X,Y,SP,STATUS,PC)
	* `R`: RAM (40 KiB)
	* `B`: Banked RAM (2 MiB)
	* `V`: Video RAM and registers (128 KiB VRAM, 32 B composer registers, 512 B palette, 16 B layer0 registers, 16 B layer1 registers, 16 B sprite registers, 2 KiB sprite attributes)
* `-sound` can be used to specify the output sound device.
* `-abufs` can be used to specify the number of audio buffers (defaults to 8). If you're experiencing stuttering in the audio try to increase this number. This will result in additional audio latency though.
* `-via2` installs the second VIA chip expansion at $9F10.
* `-midline-effects` enables mid-scanline raster effects at the cost of vastly increased host CPU usage.
* `-mhz <n>` sets the emulated CPU's speed. Range is from 1-40. This option is mainly for testing and benchmarking.
* `-enable-ym2151-irq` connects the YM2151's IRQ pin to the system's IRQ line with a modest increase in host CPU usage.
* `-wuninit` enables warnings on the console for reads of uninitialized memory.
* `-zeroram` fills RAM at startup with zeroes instead of the default of random data.
* `-version` prints additional version information of the emulator and ROM.
* When compiled with `#define TRACE`, `-trace` will enable an instruction trace on stdout.

Run `x16emu -h` to see all command line options.

Keyboard Layout
---------------

The X16 uses a PS/2 keyboard, and the ROM currently supports several different layouts. The following table shows their names, and what keys produce different characters than expected:

|Name  |Description 	       |Differences|
|------|------------------------|-------|
|en-us |US		       |[`] ⇒ [←], [~] ⇒ [π], [&#92;] ⇒ [£]|
|en-gb |United Kingdom	       |[`] ⇒ [←], [~] ⇒ [π]|
|de    |German		       |[§] ⇒ [£], [´] ⇒ [^], [^] ⇒ [←], [°] ⇒ [π]|
|nordic|Nordic                 |key left of [1] ⇒ [←],[π]|
|it    |Italian		       |[&#92;] ⇒ [←], [&vert;] ⇒ [π]|
|pl    |Polish (Programmers)   |[`] ⇒ [←], [~] ⇒ [π], [&#92;] ⇒ [£]|
|hu    |Hungarian	       |[&#92;] ⇒ [←], [&vert;] ⇒ [π], [§] ⇒ [£]|
|es    |Spanish		       |[&vert;] ⇒ π, &#92; ⇒ [←], Alt + [<] ⇒ [£]|
|fr    |French		       |[²] ⇒ [←], [§] ⇒ [£]|
|de-ch |Swiss German	       |[^] ⇒ [←], [°] ⇒ [π]|
|fr-be |Belgian French	       |[²] ⇒ [←], [³] ⇒ [π]|
|fi    |Finnish		       |[§] ⇒ [←], [½] ⇒ [π]|
|pt-br |Portuguese (Brazil ABNT)|[&#92;] ⇒ [←], [&vert;] ⇒ [π]|

Keys that produce international characters (like [ä] or [ç]) will not produce any character.

Since the host computer tells the Commander X16 via the emulator the *position* of keys that are pressed, you need to configure the layout for the X16 independently of the keyboard layout you have configured on the host.

**Use the `MENU` command to select a layout, or set the keyboard layout at startup using the `-keymap` command line argument.**

The following keys can be used for controlling games:

|Keyboard Key  | SNES Equivalent |
|--------------|-----------------|
|X or Ctrl     | A               |
|Z or Alt      | B               |
|S 	           | X               |
|A 	           | Y               |
|D 	           | L               |
|C 	           | R               |
|Shift         | SELECT          |
|Enter         | START           |
|Cursor Up     | UP              |
|Cursor Down   | DOWN            |
|Cursor Left   | LEFT            |
|Cursor Right  | RIGHT           |


Functions while running
-----------------------

#### Windows and Linux
* `Ctrl` + `F` and `Ctrl` + `Return` will toggle full screen mode.
* `Ctrl` + `M` will toggle mouse capture mode.
* `Ctrl` + `P` will write a screenshot in PNG format to disk.
* `Ctrl` + `R` will reset the computer.
* `Ctrl` + `Backspace` will send an NMI to the computer (like RESTORE key).
* `Ctrl` + `S` will save a system dump configurable with `-dump`) to disk.
* `Ctrl` + `V` will paste the clipboard by injecting key presses.
* `Ctrl` + `=` and `Ctrl` + `+` will toggle warp mode.

#### Mac OS
* `⌘F` and `⌘Return` will toggle full screen mode.
* `⇧⌘M` will toggle mouse capture mode.
* `⌘P` will write a screenshot in PNG format to disk.
* `⌘R` will reset the computer.
* `⌘Delete` aka `⌘Backspace` will send an NMI to the computer (like RESTORE key).
* `⌘S` will save a system dump (configurable with `-dump`) to disk.
* `⌘V` will paste the clipboard by injecting key presses.
* `⌘=` and `⇧⌘+` will toggle warp mode.


GIF Recording
-------------

With the argument `-gif`, followed by a filename, a screen recording will be saved into the given GIF file. Please exit the emulator before reading the GIF file.

If the option `,wait` is specified after the filename, it will start recording on `POKE $9FB5,2`. It will capture a single frame on `POKE $9FB5,1` and pause recording on `POKE $9FB5,0`. `PEEK($9FB5)` returns a 128 if recording is enabled but not active.


WAV Recording
-------------

With the argument `-wav`, followed by a filename, an audio recording will be saved into the given WAV file. Please exit the emulator before reading the WAV file.

If the option `,wait` is specified after the filename, it will start recording on `POKE $9FB6,1`. If the option `,auto` is specified after the filename, it will start recording on the first non-zero audio signal. It will pause recording on `POKE $9FB6,0`. `PEEK($9FB6)` returns a 1 if recording is enabled but not active.


Emulator I/O registers
-------------------
x16-emulator exposes registers in the range of, from `$9FB0`-`$9FBF`, which allows one to control or toggle various emulator features from within emulated code.

When writing machine code that uses these registers, good practice is to read `$9FBE` and `$9FBF` and check for their return values. If the emulator is present, those memory locations will return the ASCII/PETSCII characters "1" and "6" respectively (`$31` and `$36` hex).  After verifying that the code is running under the emulator, you can confidently use the features provided by these registers.

Several of the following registers are particularly useful for debugging. In particular, writing data to `$9FB9`, `$9FBA`, or `$9FBB` will output debug information to the console, terminal, or command prompt window from which you ran x16emu.


| Register | Read Behavior | Write Behavior |
|-|-|-|
| \$9FB0 | Returns debugger enabled flag | `0` disables, `1` enables the debugger, overriding the absence or presence of the `-debug` command line argument. |
| \$9FB1 | Returns video logging flag | `0` disables, `1` enables logging of VRAM accesses to the console |
| \$9FB2 | Returns keyboard logging flag | `0` disables, `1` enables logging of keyboard events to the console |
| \$9FB3 | Returns echo mode | `0` disables, `1` enables raw echo, `2` enables cooked (`\Xnn` for non-ASCII), and `3` enables ISO (w/ conversion to UTF-8). When on, characters sent via the `BSOUT` KERNAL call will also appear on the console. |
| \$9FB4 | Returns save-on-exit flag | `0` disables, `1` enables save-on-exit. When this option is set and the program counter reaches \$FFFF, the emulator outputs a dump of emulator state to `dump.bin` before exiting. |
| \$9FB5 | Returns GIF recorder state | `0` pauses, `1` captures a single frame, and `2` activates/resumes GIF recording. The path to the GIF file must have been passed to the `-gif` command line option in advance. |
| \$9FB6 | Returns WAV recorder state | `0` pauses, `1` enables WAV recording, and `2` sets up autostart. The path to the WAV file must have been passed to the `-wav` command line option in advance. |
| \$9FB7 | Returns emu command key flag | `0` allows, and `1` inhibits most emulator command keys. Setting this flag prevents the emulator from intercepting keystrokes such as Ctrl+V/⌘V or Ctrl+R/⌘R, allowing the Commander X16 application running inside to make use of them. |
| \$9FB8 | Latches the cpu clock counter and returns bits 0-7 | Resets the cpu clock counter to 0 |
| \$9FB9 | Returns bits 8-15 from the latched cpu clock counter value | Outputs `"User debug 1: $xx"` to the console with xx replaced by the value written. |
| \$9FBA | Returns bits 16-23 from the latched cpu clock counter value | Outputs `"User debug 2: $xx"` to the console with xx replaced by the value written. |
| \$9FBB | Returns bits 24-31 from the latched cpu clock counter value | Outputs the given character to the console. This is basically a STDOUT port for programs running in the emulator. Only printable characters are allowed. Non-printables are replaced with &#xfffd;.
| \$9FBC | - | - |
| \$9FBD | Returns the keymap index, based on the argument to the `-keymap` command line option | - |
| \$9FBE | Returns the value `$31`/ASCII "1", useful for emulator presence detection | - |
| \$9FBF | Returns the value `$36`/ASCII "6", useful for emulator presence detection | - |


BASIC and the Screen Editor
---------------------------

On startup, the X16 presents direct mode of BASIC V2. You can enter BASIC statements, or line numbers with BASIC statements and `RUN` the program, just like on Commodore computers.

* To stop execution of a BASIC program, hit the `RUN/STOP` key (`Pause`), or `Ctrl+C`.
* To insert characters, first insert spaces by pressing `Shift+Backspace` or `Insert`, then type over those spaces.
* To clear the screen, press `Shift+Home`.
* To send NMI, similar to `STOP+RESTORE` on the C64, use Ctrl+Backspace/⌘Delete. On real hardware this is done with `Ctrl+Alt+RESTORE` (`Ctrl+Alt+PrtScr`) or by pressing the NMI button.


SD Card Images
--------------

The command line argument `-sdcard` lets you attach an image file for the emulated SD card. Using an emulated SD card makes filesystem operations go through the X16's DOS implementation, so it supports all filesystem operations (including directory listing though `DOS"$` command channel commands using the `DOS` statement) and guarantees full compatibility with the real device.

Images must be greater than 32 MB in size and contain an MBR partition table and a FAT32 filesystem. The file `sdcard.img.zip` in this repository is an empty 100 MB image in this format.

On macOS, you can just double-click an image to mount it, or use the command line:

	# hdiutil attach sdcard.img
	/dev/disk2              FDisk_partition_scheme
	/dev/disk2s1            Windows_FAT_32                  /Volumes/X16 DISK
	# [do something with the filesystem]
	# hdiutil detach /dev/disk[n] # [n] = number of device as printed above

On Linux, you can use the command line:

	# sudo losetup -P /dev/loop21 disk.img
	# sudo mount /dev/loop21p1 /mnt # pick a location to mount it to, like /mnt
	# [do something with the filesystem]
	# sudo umount /mnt
	# sudo losetup -d /dev/loop21

On Windows, you can use the [OSFMount](https://www.osforensics.com/tools/mount-disk-images.html) tool. Windows VHD files can also be created using the built-in Disk Manager. Careful attention should be paid to the settings when creating and formatting the VHD: 

 * The file must be at least 32MB and must be fixed size. Expanding VHDs are not supported.
 * Use an MBR partition tables. The Commander X16 does not recognize GPT partition tables.
 * You must format the VHD with FAT32. Other file formats are not supported.
 
 This is a trick, since Fixed-size VHD files contain the data first, with the metadata in a footer at the end. Since the emulator does not read or edit that medatada, it will only work with fixed-size files that are fully populated. 


Host Filesystem Interface
-------------------------

If the system ROM contains any version of the KERNAL, and there is no SD card image attached, all accesses to the ("IEEE") Commodore Bus are intercepted by the emulator for device 8 (the default). So the BASIC statements will target the host computer's local filesystem:

      DOS"$"
      LOAD"FOO.PRG"
      LOAD"IMAGE.PRG",8,1
      SAVE"BAR.PRG"
      OPEN2,8,2,"FOO,S,R"

The emulator will interpret filenames relative to the directory it was started in. On macOS, when double-clicking the executable, this is the home directory. To specify a different path as the emulated root, you can use the `-fsroot` command line option.

To avoid compatibility problems between the PETSCII and ASCII encodings, you can

* use uppercase filenames on the host side, and unshifted filenames on the X16 side.
* use `Ctrl+O` to switch to the X16 to ISO mode for ASCII compatibility.
* use `Ctrl+N` to switch to the upper/lower character set for a workaround.

As of R42, the Host Filesystem interface (or HostFS) is the preferred method of accessing files. It does not require creating or managing an SDcard image, and it supports all of the CMDR-DOS commands. However, it is not cycle-accurate, since the emulator traps calls to DOS and performs the same actions in the host environment. If performance and hardware accuracy is required, you will want to perform final testing using an SD card image. 

Dealing with BASIC Programs
---------------------------

BASIC programs are encoded in a tokenized form when saved. They are not simply ASCII files. If you want to edit BASIC programs on the host's text editor, you need to convert it to tokenized BASIC encoding from ASCII encoding before calling `LOAD` in the emulator.

* To convert the basic file from ASCII to tokenized BASIC encoding, reboot the machine and paste the ASCII text using `Ctrl + V` (Mac: `Cmd + V`) into the terminal. You can now run the program with `RUN`, or use the `SAVE` BASIC command to write the tokenized version to the host disk.  Below is an example.
  1. Copy ASCII text from host basic file "PRG.BAS"
  2. Paste into new terminal session
  3. `SAVE"ENCODED.BAS`
  4. Now you can restart the emulator and load the encoded basic file with `LOAD"ENCODED.BAS"`
  5. Run with `RUN"ENCODED.BAS"`

* To convert BASIC to ASCII, start x16emu with the `-echo` argument, `LOAD` the BASIC file, and type `LIST`. Now copy the ASCII version from the terminal.


Using the KERNAL/BASIC environment
----------------------------------

Please see the [KERNAL/BASIC documentation](https://github.com/X16Community/x16-docs/).


Debugger
--------

The debugger requires `-debug` to start. Without it, it is disabled.

There are 2 panels you can control. The code panel, the top left half, and the data panel, the bottom half of the screen. You can also edit the contents of the registers PC, A, X, Y, and SP.

The debugger uses its own command line with the following syntax:

|Statement|Description|
|---------|----------------------------------------------------------------------------------------------------|
|d %x|Change the code panel to view disassembly starting from the address %x.|
|m %x|Change the data panel to view memory starting from the address %x.|
|v %x|Display VERA RAM (VRAM) starting from address %x.|
|b %s %d|Changes the current memory bank for disassembly and data. The %s param can be either 'ram' or 'rom', the %d is the memory bank to display (but see NOTE below!).|
|r %s %x|Changes the value in the specified register. Valid registers in the %s param are 'pc', 'a', 'x', 'y', and 'sp'. %x is the value to store in that register.|

NOTE. To disassemble or dump memory locations in banked RAM or ROM, prepend the bank number to the address; for example, "m 4a300" displays memory contents of BANK 4, starting at address $a300.  This also works for the 'd' command.

The debugger keys are similar to the Microsoft Debugger shortcut keys, and work as follows

| Key       | Description                                                                             |
|-----------|-----------------------------------------------------------------------------------------|
| F1        | resets the shown code position to the current PC                                        |
| F2        | resets the 65C02 CPU but not any of the hardware.                                       |
| F5        | close debugger window and return to Run mode, the emulator should run as normal.        |
| F9        | sets the breakpoint to the currently code position.                                     |
| F10       | steps 'over' routines - if the next instruction is JSR it will break on return.         |
| F11       | steps 'into' routines.                                                                  |
| F12       | is used to break back into the debugger. This does not happen if you do not have -debug |
| PAGE UP   | is used to scroll up in the debugger.                                                   |
| PAGE DOWN | is used to scroll down in the debugger.                                                 |
| TAB       | when stopped, or single stepping, hides the debug information when pressed              |

When `-debug` is selected the STP instruction (opcode $DB) will break into the debugger automatically.

Keyboard routines only work when the emulator is running normally. Single stepping through keyboard code will not work at present.

CRT File Format
---------------

The Commander X16 will support cartridge ROMs, including auto-booting game cartridges. On the Gen-1 Developer board, the first slot will be used for cartridges. On the Gen-2 console machine, there is only one slot. ROM carts should work on both systems. 

This CRT format is intended for the emulator, and it is not required or used by the hardware. You can, however, use the MakeCart tool to convert between a single CRT file and BIN files that can be used to program a ROM burner. Also, note that this is different from the CRT format used the VICE emualtor, so files are not interchangable.

Commander X16 cartridges will occupy the same address space as the Commander's KERNAL and BASIC ROMs. You can control the active bank by writing to address $0001 on the computer. Banks 0-31 are the built-in ROM banks, and banks 32-255 will select the cartridge ROMs. 

### Header Layout

This is the cartridge header. The first 256 bytes are ASCII data and Human readable. The second 256 bytes are bank data; these are byte integers. Text fields are set to 16 or 32-byte boundaries for ease of formatting.

| Location | Length | Description                                                                                        |
|----------|--------|----------------------------------------------------------------------------------------------------|
| 00-15    | 16     | ASCII text: CX16 CARTRIDGE\r\n                                                                     |
| 16-31    | 16     | CRT format version. ASCII digits in format 01.02, space padded.                                    |
| 32-63    | 32     | Name. ASCII text.                                                                                  |
| 64-95    | 32     | Programmer/Developer. ASCII text.                                                                  |
| 96-127   | 32     | Copyright information. ASCII text.                                                                 |
| 128-191  | 32     | Program version. ASCII text.                                                                       |
| 192-255  | 64     | Empty.                                                                                             |
| 256-287  | 32     | Fill with zeros.                                                                                   |
| 288-511  | 224    | Bank Flags.                                                                                        |
|          |        | 00: Not Present. No data is present in the emulator or in the file.                                |
|          |        | 01: ROM: 16KB of ROM data. Data is write protected in emulator.                                    |
|          |        | 02: RAM: No data in file. Bank is read/write in emulator.                                          |
|          |        | 03: RAM: Data present: data is loaded from the file and discarded on shutdown. Useful for testing. |
|          |        | 04: NVRAM: No Data in file. Memory is writeable. Emulator saves data to NVRAM file.                |
|          |        | 05: NVRAM: Data present. Memory is writeable. Emulator saves data to NVRAM file.                   |
| 512-end  |        | Payload data.                                                                                      |
|          |        | 16384 bytes per bank for types 1, 3, and 5.                                                        |
|          |        | 0 bytes for types 0,2, and 4.                                                                      |


For NVRAM banks: on shutdown, the emulator will write out an NVRAM file that contains the data of all of the NVRAM banks. The next time this cartridge is started, the NVRAM file will be loaded into any NVRAM bank. This overwrites any data present in NVRAM banks in the CRT file. 

For types 00, 02, and 04: The file does *not* contain data for these bank types. Instead, the file skips straight to the next bank with initialized data (01, 03, or 05). 

For all "No Data" banks, the data in RAM is *undefined*. While the emulator currently initializes RAM to 0 bytes, the hardware will have random values. In addition, unpopulated addresses will be "open collector" and will have unpredicatable results. 

### Vectors

X16 hardware, and thus the emulator, will only read 6502 vectors out of bank 0. This is done via the CPU's VPB pin being connected to the ROM bank latch reset pin. In the past specific vectors were recommended in cartridge ROMs, but this is no longer true. In cartridges, the addresses `$FFFA`-`$FFFF` are free to use for data.


<!-- For PDF formatting -->
<div class="page-break"></div>

## MakeCart Conversion Tool

A conversion tool to pack cartridge data into a CRT file, `makecart`, is included in this release.

`-cfg <filename.cfg>` 
Use this file to pack the cartridge data. Config file is simply the command line switches, one per line.

`-desc "Name/Description"` 
Set the description field of the cartridge file. Up to 32 bytes of ASCII text.

`-author "Author Information"`
Set the author information field of the cartridge file. Up to 32 bytes of ASCII text.

`-copyright "Copyright Information"`
Set the copyright information field of the cartridge file. Up to 32 bytes of ASCII text.

`-version "version"`
Set the version information field of the cartridge file. Up to 32 bytes of ASCII text.

`-fill <value>` 
Set the fill value to use with any partially-filled banks of cartridge memory. Value can be defined in decimal, or in hexadecimal with a '$' or '0x' prefix. 8-bit values will be repeated every byte, 16-bit values every two bytes, and 32-bit values every 4 bytes.

`-rom_file <start_bank> [<filename.bin> [<filename.bin>] ... ]` 

Define rom banks from the specified list of files. File data is tightly packed -- if a file does not end on a 16KB interval, the next file will be inserted immediately after it within the same bank. If the last file does not end on a 16KB interval, the remainder of the rom will be filled with the value set by '-fill'. 

Valid bank numbers are 32 - 255.

`-ram <start_bank> [<end bank>]`
Define one or more banks of RAM. RAM banks are not included in the payload.

`-ram_file <start_bank> [<filename.bin> [<filename.bin>] ... ]`
Define one or more banks of initialized RAM. Note that Initialized RAM banks are not saved to the NVRAM file at shutdown. 

`-nvram <start_bank> [<end_bank>]`
Define one or more uninitalized nvram banks.

`-nvram_value <start_bank> <end_bank>`
Define pre-initialized nvram banks with the value set by '-fill'. Repeated payload bytes will be written to the file.

`-nvram_file <start_bank> [<filename.bin> [<filename.bin>] ... ]`

Define pre-initialized nvram banks from the specified list of files. File data is tightly packed like with -rom. If the last file does not end on a 16KB interval, the remainder of the rom will be filled with the value set by '-fill'.

`-none <start_bank> [<end_bank>]`
Define one or more unpopulated banks of the cartridge. By default, all banks are unpopulated unless specified by a previous command-line option. These banks are not present in the payload and only popualte the bank header in the CRT file.

`-o <output.crt>`
Set the filename of the output cartridge file.

All options can be specified multiple times, and are applied  in-order from left to right. For -desc and -o, it is legal to specify them multiple times but only the right-most instances of each will have effect.

`-unpack <input.crt> [<rom_size>]` 
Unpacks the binary data from the cartridge file into `<rom_size>` slices. (for use with an EPROM programmer.) The ouptut files will be the same filename as the input file, with _### appended. This will also create a .cfg file that can be used to re-pack the files into a new CRT if needed. 

The config file is just a series of command-line switches, with one item per line. This example assumes ladder.bin uses 3 banks, for a total of 48K, and that each level map is 4KB in size.

```
-o ladder.crt
-name "Ladder"
-author "Yahoo Software"
-copyright "(c) 1982, 1983 Yahoo Software"
-version "1.30TP" 
-rom_file 32 ladder.bin
-rom_file 35 level_01.bin level_02.bin level_03.bin level_04.bin
-nvram 37
-fill 0
```

This would create file with 

* 512 byte header
* 5 ROM banks 
  * 3 for the 48K ladder.bin 
  * 1 for the four 4KB level files.
* 1 empty NVRAM bank

Since the NVRAM bank is not initialized, it is not included in the file. This makes the file a total of 66,048 bytes long. (512 bytes, plus four 16KB banks.)


Web Site
--------

[https://commanderx16.com](https://commanderx16.com)

Forum
-----

[https://cx16forum.com/forum](https://cx16forum.com/forum/)


License
-------

Copyright (c) 2019-2023 Michael Steil &lt;mist64@mac.com&gt;, [www.pagetable.com](https://www.pagetable.com/), et al.
All rights reserved. License: 2-clause BSD


Release Notes
-------------
See [RELEASES](RELEASES.md#releases).


<!-------------------------------------------------------------------->
[releases]: https://github.com/X16Community/x16-emulator/releases
[webassembly]: http://github.com/X16Community/x16-emulator/blob/master/webassembly/WebAssembly.md
[x16rom-build]: https://github.com/X16Community/x16-rom#releases-and-building
[x16rom]: https://github.com/X16Community/x16-rom

<!-- For PDF formatting -->
<div class="page-break"></div>
