
<p align="center">
  <img src="./.gh/logo.png" />
</p>

## Releases

### Release 46 ("Winnipeg")

This is mainly a bugfix release.

* Features/Fixes
	* A change in the ROM caused controllers to have their buttons offset due to how the emulated VIA responds to polling. The issue was fixed in the ROM and while it was not technically an emulator bug and no changes were made to the emulator's joystick routines, it gets a mention here.
	* HostFS: UNLSN was incorrectly setting the KERNAL status byte based on whether a file existed upon open. It has been changed to behave like CMDR-DOS. (discovered by [voidstar78])
	* HostFS: Opening a file in Modify mode now properly creates file if it doesn't yet exist (discovered by [m00dawg])
	* Audio: prevent `-sound none` from trying to use the uninitialized ymfm YM2151 library. The YM status register was also modified to always return 0 in this case. (discovered by [jestin])
	* WebAssembly: improve handling of zip files without `manifest.json` [Cyber-Ex]
	* GUI: If emulator launches in a window with the titlebar offscreen, move the window so that it is visible. (with help from [irmen])
	* GUI: Mouse movement now uses relative motion in capture mode, which makes the position of the host mouse irrelevant while in capture mode.
	* GUI: new `-capture` command line option to start emulator with mouse captured
	* Testbench: miscellaneous fixes [irmen]
	* More changes in the [ROM](https://github.com/X16Community/x16-rom/tree/r46#release-46-winnipeg).

### Release 45 ("Nuuk")

This is a minor release with respect to the emulator.  The bulk of the changes are in the [ROM](https://github.com/X16Community/x16-rom/tree/r45#release-45-nuuk).

* Features/Fixes
	* Revert VERA PSG amplitude resolution back to 6 bits. This was upped previously to match VERA firmware. It was subsequently reverted in VERA to make room for the FX feature. [akumanatt]
	* Intellimouse support added to the emulated SMC, partially implementing the new feature in hardware SMC firmware 45.1.0. [stefan-b-jakobsson]
		* Scroll wheel is supported (mouse device ID 3)
		* Not implemented: Extra buttons (mouse device ID 4)
	* New emulator debug register behaviors
		* Reading from \$9FB8-\$9FBB in this order returns the 32-bit CPU clock counter, snapshotted at the time \$9FB8 is read. Previously the clock counter would remain in motion and the upper counter bits could roll over unpredictably.
		* Writing to \$9FB8-\$9FBB has new behaviors:
			* \$9FB8: resets the cpu clock counter to 0.
			* \$9FB9: prints a debug message to the console `User debug 1: $xx`.
			* \$9FBA: prints a debug message to the console `User debug 2: $xx`.
			* \$9FBB: prints the UTF-8 representation of the ISO character to the console. This can be treated like a debug STDOUT.
		* Before using any of these emulator debug registers, it's recommended to test for emulator presence first.
			* Read from `$9FBE` and `$9FBF`. When running under the emulator, the returned values should be `$31` and `$36` respectively. If any other values are returned, you can usually assume to be running on real hardware. While the stock machine doesn't have any I/O devices that listen to the emulator I/O range, an add-on card could choose to use that same address space in the future for its own functions.
	* New MCIOUT (blockwise write) implementation for HostFS, mirroring the feature in the kernal for use on SD card.
	* New command key and capture behavior:
		* Ctrl+M/⇧⌘M is always processed by the emulator to toggle (mouse/keyboard) capture, regardless of the state of the "Disable Emulator Keys" flag.
		* Turning capture mode on disables all other emulator keys until capture mode is toggled off. While capture is on, the emulator also routes most OS shortcut key combos to the X16: for instance, Alt+Tab.


### Release 44 ("Milan")

This is the third release of x16-emulator by the X16Community team

* Features/Fixes
	* Many changes to HostFS, including
		* Fix regression for loading "`:*`" from HostFS while also using an SD card image
		* Fix `-prg` and `-sdcard` options working together, which did not properly handle the case after emulator reset
		* Proper wildcard behavior in dir filters and OPEN string
		* Fix filetype directory filter command parsing
		* Add `$=L` long mode directory listing to emulate the new feature in the ROM.
		* Speed up directory filetype filter
		* Add CMD/SD2IEC style directory navigation: `CD:←` to enter parent directory
		* Partial emulation of case-insensitivity in filenames on case-sensitive host filesystems.
			* This works in `OPEN` strings and directory names in commands (`CD:`, etc.) which do not contain a `/` character.
			* It also works on the last path segment in relative or absolute paths in directory names or `OPEN` strings. In other words, if given a path specification containing one or more `/` characters, it will do only a case-insensitive search on the part after the final `/`.
		* Proper translation between UTF-8 filenames and their ISO representations.
	* Implement VPB behavior to match hardware [akumanatt]
		* Fix BRK to have VPB behavior
	* Support for additional keycodes (NumLock, Menu) [stefan-b-jakobsson]
	* Fix debugger to set the correct bank for breakpoints [gaekwad]
	* New `-fullscreen` CLI option
	* Proper cleanup when the emulator exits when in full screen [irmen]
	* FX emulation, which mirrors the features in the FX enhancement to the VERA firmware. See the [VERA FX Reference](https://github.com/X16Community/x16-docs/blob/master/VERA%20FX%20Reference.md) for details.
	* Writing bit 6 and bit 7 together into VERA_AUDIO_CTRL now enables looping the PCM FIFO (and does not reset the FIFO). Any other write into VERA_AUDIO_CTRL disables looping.
	* New `-opacity` CLI option for window transparency [tstibor]
	* New support for screenshots (Ctrl+P/⌘P) [dressupgeekout]
	* Fix small memory leak caused by pasting into the emulator
	* Use relative mouse motion while in grabbed mode
	* Remove `-geos` CLI option [dressupgeekout]
	* New YM2151 audio core: remove old MAME core, replace with ymfm
		* Allows for IRQs from the YM, requires specifying `-enable-ym2151-irq` on the command line
	* Emulate hardware open bus behavior when reading from a device that doesn't exist in the `$9Fxx` space
	* Reset via I2C command: defer machine reset to the main loop, which allows the I2C write routine to return cleanly.
	* Fix 65C02 `BIT` immediate behavior [XarkLabs]
	* New NMI trigger emulator hotkey, emulates Ctrl+Alt+Restore on hardware (Ctrl+Backspace/⌘Delete) [XarkLabs]
	* Fix line artifact in application icon/logo
	* Grabbing the mouse with (Ctrl+M/⇧⌘M) now grabs the keyboard as well. It allows the emulator to receive keystrokes and key combinations which would otherwise be intercepted by the operating system.
	* Fix description of fill value in `makecart`
	* New features implemented in the [ROM](https://github.com/X16Community/x16-rom/tree/r44#release-44-milan)
* Build
	* Link-time optimization is now enabled by default
	* Portability enhancements [dressupgeekout]
	* Suppress clang warnings due to deprecated sprintf usage in ymfm lib [XarkLabs]

### Release 43 ("Stockholm")

This is the second release of x16-emulator by the X16Community team

* **BREAKING CHANGE**
	* The keyboard protocol between the emulated SMC and the KERNAL has changed, thus x16-emulator version R43 requires x16-rom version R43.
	* This change also affects how the custom keyboard handler vector works (keyhdl). For details, see [Chapter 2 of the Programmer's Reference Guide](https://github.com/X16Community/x16-docs/blob/master/X16%20Reference%20-%2002%20-%20Editor.md#custom-keyboard-keynum-code-handler)
	* **Your Keyboard will not work unless** you are running
		* R43 of both x16-rom and x16-emulator
* Features
	* Updates to support translation from SDL scancodes to new keynum encoding supported by KERNAL [stefan-b-jakobsson]
	* More granular support for RAM amount as argument to `-ram`
	* Minor HostFS bugfixes and enhancements, including tying the activity light to HostFS activity.
	* VERA updates: new support for 240p in NTSC/RGB modes. Chroma disable only works on NTSC.
	* Stepping the debugger now supports stepping over `WAI`
	* Debugger now shows the correct bank in the disassembly by default. [gaekwad]
	* Debugger breakpoints are now bank-specific [gaekwad]
	* Randomized RAM is now the default. New option: `-zeroram` [irmen]
	* Host's mouse cursor is now shown unless either the KERNAL mouse is enabled or the mouse cursor is captured (Ctrl+M/⇧⌘M).
	* Esc key is now Esc rather than STOP.  Pause key sends STOP.  (Ctrl+C is also recognized by the KERNAL as STOP)
	* SD card emulation now responds to CMD9
	* Emulated SMC can now assert NMI.
	* Add `-mhz` option to select a speed other than 8
	* When built with `TRACE`, the `-trace` output now shows the effective address for indirect and indexed opcodes and VERA data0/data1 reads and writes.
	* New comamnd line option `-midline-effects` that supports mid-line changes to the palette or tile/sprite data. R42 always had this behavior, which results in performance degradation for programs write to VERA heavily if the host CPU is not fast enough. This behavior is now disabled by default. `-midline-effects` restores this optional behavior.
	* New features implemented in the [ROM](https://github.com/X16Community/x16-rom/tree/r43#release-43-stockholm)
* Other
	* Release builds have link-time optimization enabled which seems to help performance.
	* Add git hash of build to `-version` string.
	* WebAssembly enhancements in the supporting html/js [Cyber-EX]
	* Fixed potential off-by one row with non-zero DC_VSTART.
	* Prevent laggy hostfs reads from causing the emulator to warp to catch up by translating the wall clock time to elapsed 6502 clocks. This effectively makes HostFS MACPTR behave like a DMA card, including the possibility that it prevents the CPU from executing instructions while interrupt sources may have been waiting for service.
	* Bugfix: Process multiple SDL events per frame. (Fixed choppy mouse movement if there were keystrokes in the keyboard buffer)
	* Audio resampling and ring buffer fixes [DragWx]
	* Build fixes on Mac
### Release 42 ("Cambridge")

This is the first release of x16-emulator by the X16Community team

* Features
	* Added testbench mode [stefan-b-jakobsson, indigodarkwolf]
	* Added `-noemucmdkeys` option [jestin]
	* New `FIFO_EMPTY` flag in `PCM_CTRL` to reflect new VERA feature [ZeroByteOrg]
	* Added `-widescreen` option to simulate stretched 640x480 output at a 16:9 aspect ratio [jestin]
	* New `SCANLINE` VERA register behavior to reflect updated VERA feature [mooinglemur]
	* Added `-randram` and `-wuninit` command line arguments to randomize RAM at boot, and to emit a console warning when uninitialized RAM is read, respectively. [stefan-b-jakobsson]
	* Allow specifying non-power-of-2 argument to `-ram`, in increments of 8k [JimmyDansbo]
	* Added `-via2` option to selectively enable a VIA at $9F10. [akumanatt]
	* Added ROM cart loading with `-cart` and `-cartbin` [indigodarkwolf]
	* New `makecart` utility for building `.crt` cartridge files [indigodarkwolf]
	* Compressed SD card image support [indigodarkwolf]
	* Mouse grab mode, press Ctrl+M (Mac: ⇧⌘M) to toggle. [mooinglemur]
	* New `-fsroot` and `-startin` options to specify the root of the emulated host fs, and the host directory to start in respectively. [mooinglemur]
	* Many, many new features implemented in the [ROM](https://github.com/X16Community/x16-rom/tree/r42#release-42-cambridge)
* Other
	* PS/2 devices now connected via SMC via I2C, I2C pins have moved to match hardware [stefan-b-jakobsson]
	* Recognize middle mouse button [ZeroByteOrg]
	* Synchronized keymaps with ROM [megagrump]
	* Build fixes [irmen]
	* Show dialog when a `STP` instruction is encountered with debug turned off [akumanatt]
	* Improved emulated behavior of `WAI` [LRFLEW]
	* Clear D flag on interrupt entry [LRFLEW]
	* Update BRK length in debugger [indigodarkwolf]
	* IRQ/NMI entry clock cycles are now accounted for [mooinglemur]
	* Add reason string to memory dump output [irmen]
	* Clear sprite line buffer when disabling sprite layer [jestin]
	* Improved audio balance between VERA and YM2151. Much improved mixing routines to reduce stutters and clicking. [akumanatt]
	* To match hardware, VERA ISR bits are set at VSYNC, LINE, and SPRCOL regardless of whether their respective IEN bits are set [mooinglemur]
	* Changes to match Proto 4, including moving VIA1 interrupt pin to IRQ [akumanatt]
	* VERA mid-frame raster effects more closely match the timing of real hardware [mooinglemur]
	* Enabled and built out CI/CD build workflows [maxgerhardt, indigodarkwolf, mooinglemur]
	* Many host fs enhancements, bringing host fs very close to feature parity with SD card images [davidgiven, ZeroByteOrg, mooinglemur]
	* Many documentation updates and fixes [veganaize, irmen, tomxp411]

### Release 41 ("Marrakech")

* allow apps to intercept Cmd/Win, Menu and Caps-Lock keys
* fixed `-prg` with `-sdcard`
* fixed loading from host filesystem (length reporting by `MACPTR` on EOI)
* macOS: support for older versions like Catalina (10.15)

### Release 40 ("Bonn")

* Features
	* improved VERA video timings [Natt Akuma]
	* added Host FS bridging using IEEE API
	* added Serial Bus emulation [experimental]
	* added WAV file recording [Stephen Horn]
	* possible to disable Ctrl/Cmd key interception ($9FB7) [mooinglemur] 
* Other
	* Fixed I2C (RTC, SMC)
	* Fixed RAM/ROM bank for PC when entering break [mjallison42]
	* LST support for -trace

## Release 39 ("Buenos Aires")

* Switch to Proto2 Hardware
	* banking through zp addresses 0 and 1
	* modified I/O layout
	* modified VIA GPIO layout
	* support for 4 controllers
	* I2C bus with SMC and RTC/NVRAM
* Features
	* implemented VIA timers [Natt Akuma]
	* added option to disable sound [Jimmy Dansbo]
	* added support for Delete, Insert, End, PgUp and PgDn keys [Stefan B Jakobsson]
	* debugger scroll up & down description [Matas Lesinskas]
	* added anti-aliasing to VERA PSG waveforms [TaleTN]
* Bugs
	* fixed sending only one mouse update per frame [Elektron72]
	* fixed VSYNC timing [Elektron72]
	* switched front and back porches [Elektron72]
	* fixed LOAD/SAVE hypercall so debugger doesn't break [Stephen Horn]
	* fixed YM2151 frequency from 4MHz ->3.579545MHz [Stephen Horn]
	* do not set compositor bypass hint for SDL Window [Stephen Horn]
	* reset timing after exiting debugger [Elektron72]
	* don't write nvram after every frame
	* fixed write outside of line buffer [Stephen Horn]
	* fixed BRA extra CPU cycle [LRFLEW]
	* fix: clear layer line once layer is disabled
	* fixed BBSx/BBRx timing [Natt Akuma]
* Other
	* misc speed optimizations [Stephen Horn]

## Release 38 ("Kyoto")

* CPU
	* added WAI, BBS, BBR, SMB, and RMB instructions [Stephen Horn]
* VERA
	* VERA speed optimizations [Stephen Horn]
	* fixed raster line interrupt [Stephen Horn]
	* added sprite collision interrupt [Stephen Horn]
	* fixed sprite wrapping [Stephen Horn]
	* added VERA dump, fill commands to debugger [Stephen Horn]
	* fixed VRAM memory dump [Stephen Horn]
* SD card
	* SD card write support
	* Ctrl+D/Cmd+D detaches/attaches SD card (for debugging)
	* improved/cleaned up SD card emulation [Frank van den Hoef]
	* SD card activity/error LED support
	* VERA-SPI: support Auto-TX mode
* misc
	* added warp mode (Ctrl+'+'/Cmd+'+' to toggle, or `-warp`)
	* added '-version' shell option [Alice Trillian Osako]
	* new app icon [Stephen Horn]
	* expose 32 bit cycle counter (up to 500 sec) in emulator I/O area
	* zero page register display in debugger [Mike Allison]
	* Various WebAssembly improvements and fixes [Sebastian Voges]

### Release 37 ("Geneva")

* VERA 0.9 register layout [Frank van den Hoef]
* audio [Frank van den Hoef]
    * VERA PCM and PSG audio support
    * YM2151 support is now enabled by default
    * added `-abufs` to specify number of audio buffers
* removed UART [Frank van den Hoef]
* added window icon [Nigel Stewart]
* fixed access to paths with non-ASCII characters on Windows [Serentty]
* SDL HiDPI hint to fix mouse scaling [Edward Kmett]

### Release 36 ("Berlin")

* added VERA UART emulation (`-uart-in`, `-uart-out`)
* correctly emulate missing SD card
* moved host filesystem interface from device 1 to device 8, only available if no SD card is attached
* require numeric argument for `-test` to auto-run test
* fixed JMP (a,x) for 65c02
* Fixed ESC as RUN/STOP [Ingo Hinterding]

### Release 35

* video optimization [Neil Forbes-Richardson]
* added `-geos` to launch GEOS on startup
* added `-test` to launch (graphics) unit test on startup
* debugger
	* switch viewed RAM/ROM bank with `numpad +` and `numpad -` [Kobrasadetin]
	* optimized character printing [Kobrasadetin]
* trace mode:
	* prepend ROM bank to address in trace
	* also prints 16 bit virtual regs (graph/GEOS)
* fixes
	* initialize memory to 0 [Kobrasadetin]
	* fixed SYS hex argument
	* disabled "buffer full, skipping" and SD card debug text, it was too noisy

### Release 34

* PS/2 mouse
* support for text mode with tiles other than 8x8 [Serentty]
* fix: programmatic echo mode control [Mikael O. Bonnier]

### Release 33

* significant performance optimizations
* VERA
	* enabled all 128 sprites
	* correct sprite zdepth
	* support for raster IRQs
* SDL controller support using `-joy1` and `-joy2` [John J Bliss]
* 65C02 BCD fixes [Norman B. Lancaster]
* feature parity with new LOAD/VLOAD features [John-Paul Gignac]
* default RAM and ROM banks are now 0, matching the hardware
* GIF recording can now be controlled from inside the machine [Randall Bohn]
* Debugging
	* Major enhancements to the debugger [kktos]
	* `-echo` will now encode non-printable characters like this: \X93 for CHR$(93), `-bas` as 	well as pasting accepts this convention again
	* `-echo raw` for the original behavior
	* `-echo iso` for correct character encoding in ISO mode
	* `-ram` to specify RAM size; now defaults to 512

### Release 32

* correct ROM banking
* VERA emulation optimizations [Stephen Horn]
* added `-dump` option to allow writing RAM, CPU state or VERA state to disk [Nils Hasenbanck]
* added `-quality` option to change scaling algorithm; now defaults to "best" [Maurizio Porrato]
* output of `-echo` can now be fed into UNIX pipes [Anonymous Maarten]
* relative speed of emulator is shown in the title if host can't keep up [Rien]
* fix: 6502 BCD arithmetic [Rien]
* fix: colors (white is now white) [Rien]
* fix: sprite flipping [jjbliss]

### Release 31

* VERA 0.8 register layout
* removed `-char` (character ROM is now part of `rom.bin`)
* GIF recording using `-gif` [Neil Forbes-Richardson]
* numpad support [Maurizio Porrato]
* fake support of VIA timers to work around BASIC RND(0)
* default ROM is taken from executable's directory [Michael Watters]
* emulator window has a title [Michael Watters]
* `-debug` allows specifying a breakpoint [Frank Buss]
* package contains the ROM symbols in `rom.txt`
* support for VERA SPI

### Release 30

Emulator:
* VERA can now generate VSYNC interrupts
* added `-keymap` for setting the keyboard layout
* added `-scale` for integer scaling of the window [Stephen Horn]
* added `-log` to enable various logging features (can also be enabled at runtime (POKE $9FB0+) [Randall Bohn])
* changed `-run` to be an option to `-prg` and `-bas`
* emulator detection: read $9FBE/$9FBF, must read 0x31 and 0x36
* fix: `-prg` and `-run` no longer corrupt BASIC programs.
* fix: `LOAD,1` into RAM bank [Stephen Horn]
* fix: 2bpp and 4bpp drawing [Stephen Horn]
* fix: 4bpp sprites [MonstersGoBoom]
* fix: build on Linux/ARM

### Release 29

* better keyboard support: if you pretend you have a US keyboard layout when typing, all keys should now be reachable [Paul Robson]
* `-debug` will enable the new debugger [Paul Robson]
* runs at the correct speed (was way too slow on most machines)
* keyboard shortcuts work on Windows/Linux: `Ctrl + F/R/S/V`
* `Ctrl + V` pastes the clipboard as keypresses
* `-bas file.txt` loads a BASIC program in ASCII encoding
* `-echo` prints all BASIC/KERNAL output to the terminal, use it with LIST to convert a BASIC program to ASCII
* `-run` acts like `-prg`, but also autostarts the program
* `JMP $FFFF` and `SYS 65535` exit the emulator and save memory into the host's storage
* the packages now contain the current version of the Programmer's Reference Guide (HTML)
* fix: on Windows, some file load/saves may be been truncated

### Release 28

* support for 65C02 opcodes [Paul Robson]
* keep aspect ratio when resizing window [Sebastian Voges]
* updated sprite logic to VERA 0.7 – **the layout of the sprite data registers has changed, you need to change your code!**


### Release 27

* Command line overhaul. Supports `-rom`, `-char`, `-sdcard` and `-prg`.
* ROM and char filename defaults, so x16emu can be started without arguments.
* Host Filesystem Interface supports `LOAD"$"`
* macOS and Windows packaging logic in Makefile

### Release 26

* better sprite support (clipping, palette offset, flipping)
* better border support
* KERNAL can set up interlaced NTSC mode with scaling and borders (compile time option)

### Release 25

* sdcard: fixed `LOAD,x,1` to load to the correct addressg
* sdcard: all temp data will be on bank #255; current bank will remain unchanged
* DOS: support for DOS commands ("UI", "I", "V", ...) and more status messages (e.g. 26,WRITE PROTECT ON,00,00)
* BASIC: `DOS` command. Without argument: print disk status; with "$" argument: show directory; with "8" or "9" argument: switch default drive; otherwise: send DOS command; also accessible through F7/F8
* Vera: cycle exact rendering, NTSC, interlacing, border

### Release 24

* SD card support
	* pass path to SD card image as third argument
	* access SD card as drive 8
	* the local PC/Mac disk is still drive 1
	* modulo debugging, this would work on a real X16 with the SD card (plus level shifters) hooked up to VIA#2PB as described in sdcard.c in the emulator surce

### Release 23

* Updated emulator and ROM to spec 0.6 – the ROM image should work on a real X16 with VERA 0.6 now.

### Release 22

SYS65375 (SWAPPER) now also clears the screen, avoid ing side effects.

### Release 21

* support for $ and % number prefixes in BASIC
* support for C128 KERNAL APIs LKUPLA, LKUPSA and CLOSE_ALL

### Release 20

* Toggle fullscreen using `Cmd + F` or `Cmd + return`
* new BASIC instructions and functions:
	* `MON`: enter monitor; no more SYS65280 required
	* `VPEEK(bank, address)`
	* `VPOKE bank, address, value`
example: `VPOKE4,0,VPEEK(4,0) OR 32` [for 256 color BASIC]

### Release 19

* fixed cursor trail bug
* fixed f7 key in PS/2 driver
* f keys are assigned with shortcuts now:
F1: LIST
F2: &lt;enter monitor&gt;
F3: RUN
F4: &lt;switch 40/80&gt;
F5: LOAD
F6: SAVE"
F7: DOS"$ &lt;doesn't work yet&gt;
F8: DOS &lt;doesn't work yet&gt;

### Release 18

* Fixed scrolling in 40x30 mode when there are double lines on the screen.

### Release 17

* video RAM support in the monitor (SYS65280)
* 40x30 screen support (SYS65375 to toggle)

### Release 16

* Integrated monitor, start with SYS65280
`rom.bin` is now 3*8 KB:
	* 0: BASIC (bank 0 at $C000)
	* 1: KERNAL ($E000)
	* 2: UTIL (bank 1 at $C000)

### Release 15

* correct text mode video RAM layout both in emulator and KERNAL

### Release 14

* KERNAL: fast scrolling
* KERNAL: upper/lower switching using CHR$($0E)/CHR$($8E)
* KERNAL: banking init
* KERNAL: new PS/2 driver
* Emulator: VERA updates (more modes, second data port)
* Emulator: RAM and ROM banks start out as all 1 bits

### Release 13

* Supports mode 7 (8bpp bitmap).

### Release 12

* Supports 8bpp tile mode (mode 4)

### Release 11

* The emulator and the KERNAL now speak the bit-level PS/2 protocol over VIA#2 PA0/PA1. The system behaves the same, but keyboard input in the ROM should work on a real device.

### Release 10

updated KERNAL with proper power-on message

### Release 9

* LOAD and SAVE commands are intercepted by the emulator, can be used to access local file system, like this:

      LOAD"TETRIS.PRG
      SAVE"TETRIS.PRG

* No device number is necessary. Loading absolute works like this:

      LOAD"FILE.PRG",1,1

### Release 8

* New optional override load address for PRG files:

      ./x64emu rom.bin chargen.bin basic.prg,0401

### Release 7

* Now with banking. `POKE40801,n` to switch the RAM bank at $A000. `POKE40800,n` to switch the ROM bank at $C000. The ROM file at the command line can be up to 72 KB now (layout: 0: bank 0, 1: KERNAL, 2: bank 1, 3: bank 2 etc.), and the RAM that `Cmd + S` saves is 2088KB ($0000-$9F00: regular RAM, $9F00-$9FFF: unused, $A000+: extra banks)

### Release 6

* Vera emulation now matches the complete spec dated 2019-07-06: correct video address space layout, palette format, redefinable character set

### Release 5

* BASIC now starts at $0401 (39679 BASIC BYTES FREE)

### Release 4

* `Cmd + S` now saves all of memory (linear 64 KB for now, including ROM) to `memory.bin`, `memory-1.bin`, `memory-2.bin`, etc. You can extract parts of it with Unix "dd", like: `dd if=memory.bin of=basic.bin bs=1 skip=2049 count=38655`

### Release 3

* Supports PRG file as third argument, which is injected after "READY.", so BASIC programs work as well.

### Release 2

* STOP key support

### Release 1

* 6502 core, fake PS/2 keyboard emulation (PS/2 data bytes appear at VIA#1 PB) and text mode Vera emulation
* KERNAL/BASIC modified for memory layout, missing VIC, Vera text mode and PS/2 keyboard
