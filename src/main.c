// Commander X16 Emulator
// Copyright (c) 2019,2022 Michael Steil
// All rights reserved. License: 2-clause BSD

#ifndef __APPLE__
#define _XOPEN_SOURCE   600
#define _POSIX_C_SOURCE 1
#endif
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#ifdef __MINGW32__
#include <ctype.h>
#endif
#include "cpu/fake6502.h"
#include "timing.h"
#include "disasm.h"
#include "files.h"
#include "memory.h"
#include "video.h"
#include "via.h"
#include "serial.h"
#include "i2c.h"
#include "rtc.h"
#include "smc.h"
#include "vera_spi.h"
#include "sdcard.h"
#include "ieee.h"
#include "glue.h"
#include "debugger.h"
#include "utf8.h"
#include "iso_8859_15.h"
#include "joystick.h"
#include "rom_symbols.h"
#include "ymglue.h"
#include "audio.h"
#include "version.h"
#include "wav_recorder.h"
#include "testbench.h"
#include "cartridge.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <pthread.h>
#endif

void *emulator_loop(void *param);
void emscripten_main_loop(void);

// This must match the KERNAL's set!
char *keymaps[] = {
	"en-us",
	"en-us-int",
	"en-gb",
	"sv",
	"de",
	"da",
	"it",
	"pl",
	"nb",
	"hu",
	"es",
	"fi",
	"pt-br",
	"cz",
	"jp",
	"fr",
	"de-ch",
	"en-us-dvo",
	"et",
	"fr-be",
	"fr-ca",
	"is",
	"pt",
	"hr",
	"sk",
	"sl",
	"lv",
	"lt",
};

#ifdef PERFSTAT
uint32_t stat[65536];
#endif

bool debugger_enabled = false;
char *paste_text = NULL;
char *clipboard_buffer = NULL;
char paste_text_data[65536];
bool pasting_bas = false;

uint16_t num_ram_banks = 64; // 512 KB default

bool log_video = false;
bool log_speed = false;
bool log_keyboard = false;
bool dump_cpu = false;
bool dump_ram = true;
bool dump_bank = true;
bool dump_vram = false;
bool warp_mode = false;
bool grab_mouse = false;
echo_mode_t echo_mode;
bool save_on_exit = true;
bool disable_emu_cmd_keys = false;
bool set_system_time = false;
bool has_serial = false;
bool no_ieee_intercept = false;
bool has_via2 = false;
gif_recorder_state_t record_gif = RECORD_GIF_DISABLED;
char *gif_path = NULL;
char *wav_path = NULL;
uint8_t *fsroot_path = NULL;
uint8_t *startin_path = NULL;
uint8_t keymap = 0; // KERNAL's default
int window_scale = 1;
float screen_x_scale = 1.0;
float window_opacity = 1.0;
char *scale_quality = "best";
bool test_init_complete=false;
bool headless = false;
bool fullscreen = false;
bool testbench = false;
bool enable_midline = false;
bool ym2151_irq_support = false;
char *cartridge_path = NULL;

bool using_hostfs = true;

uint8_t MHZ = 8;

#ifdef TRACE
bool trace_mode = false;
uint16_t trace_address = 0;
#endif

int instruction_counter;
SDL_RWops *prg_file;
bool prg_finished_loading;
int prg_override_start = -1;
bool run_after_load = false;

char *nvram_path = NULL;

#ifdef TRACE
#include "rom_labels.h"
#include "rom_lst.h"
char *
label_for_address(uint16_t address)
{
	uint16_t *addresses;
	char **labels;
	int count;
	switch (memory_get_rom_bank()) {
		case 0:
			addresses = addresses_bank0;
			labels = labels_bank0;
			count = sizeof(addresses_bank0) / sizeof(uint16_t);
			break;
		case 1:
			addresses = addresses_bank1;
			labels = labels_bank1;
			count = sizeof(addresses_bank1) / sizeof(uint16_t);
			break;
		case 2:
			addresses = addresses_bank2;
			labels = labels_bank2;
			count = sizeof(addresses_bank2) / sizeof(uint16_t);
			break;
		case 3:
			addresses = addresses_bank3;
			labels = labels_bank3;
			count = sizeof(addresses_bank3) / sizeof(uint16_t);
			break;
		case 4:
			addresses = addresses_bank4;
			labels = labels_bank4;
			count = sizeof(addresses_bank4) / sizeof(uint16_t);
			break;
		case 5:
			addresses = addresses_bank5;
			labels = labels_bank5;
			count = sizeof(addresses_bank5) / sizeof(uint16_t);
			break;
		case 6:
			addresses = addresses_bank6;
			labels = labels_bank6;
			count = sizeof(addresses_bank6) / sizeof(uint16_t);
			break;
		case 10:
			addresses = addresses_bankA;
			labels = labels_bankA;
			count = sizeof(addresses_bankA) / sizeof(uint16_t);
			break;
		case 11:
			addresses = addresses_bankB;
			labels = labels_bankB;
			count = sizeof(addresses_bankB) / sizeof(uint16_t);
			break;
		case 12:
			addresses = addresses_bankC;
			labels = labels_bankC;
			count = sizeof(addresses_bankC) / sizeof(uint16_t);
			break;
		default:
			addresses = NULL;
			labels = NULL;
	}

	if (!addresses) {
		return NULL;
	}

	for (int i = 0; i < count; i++) {
		if (address == addresses[i]) {
			return labels[i];
		}
	}
	return NULL;
}

char *
lst_for_address(uint16_t address)
{
	if (address < 0xc000) {
		return NULL;
	}

	char **lst;
	switch (memory_get_rom_bank()) {
		case 0: lst = lst_bank0; break;
		case 2: lst = lst_bank2; break;
		case 3: lst = lst_bank3; break;
		case 4: lst = lst_bank4; break;
		case 5: lst = lst_bank5; break;
		default:
			return NULL;
	}
	return lst[address - 0xc000];
}
#endif

void
machine_dump(const char* reason)
{
	printf("Dumping system memory. Reason: %s\n", reason);
	int index = 0;
	char filename[22];
	for (;;) {
		if (!index) {
			strcpy(filename, "dump.bin");
		} else {
			sprintf(filename, "dump-%i.bin", index);
		}
		if (access(filename, F_OK) == -1) {
			break;
		}
		index++;
	}
	SDL_RWops *f = SDL_RWFromFile(filename, "wb");
	if (!f) {
		printf("Cannot write to %s!\n", filename);
		return;
	}

	if (dump_cpu) {
		SDL_RWwrite(f, &a, sizeof(uint8_t), 1);
		SDL_RWwrite(f, &x, sizeof(uint8_t), 1);
		SDL_RWwrite(f, &y, sizeof(uint8_t), 1);
		SDL_RWwrite(f, &sp, sizeof(uint8_t), 1);
		SDL_RWwrite(f, &status, sizeof(uint8_t), 1);
		SDL_RWwrite(f, &pc, sizeof(uint16_t), 1);
	}
	memory_save(f, dump_ram, dump_bank);

	if (dump_vram) {
		video_save(f);
	}

	SDL_RWclose(f);
	printf("Dumped system to %s.\n", filename);
}

void mouse_state_init(void)
{
	kernal_mouse_enabled = 0;
	SDL_ShowCursor((mouse_grabbed || kernal_mouse_enabled) ? SDL_DISABLE : SDL_ENABLE);
}

void
machine_reset()
{
	i2c_reset_state();
	ieee_init();
	memory_reset();
	vera_spi_init();
	via1_init();
	if (has_via2) {
		via2_init();
	}
	video_reset();
	mouse_state_init();
	reset6502();
}

void
machine_nmi()
{
	nmi6502();
}

void
machine_paste(char *s)
{
	if (s) {
		paste_text = s;
		clipboard_buffer = s; // so that we can free this later
		pasting_bas = true;
	}
}

void
machine_toggle_warp()
{
	warp_mode = !warp_mode;
	timing_init();
}


static bool
is_kernal()
{
	return read6502(0xfff6) == 'M' && // only for KERNAL
			read6502(0xfff7) == 'I' &&
			read6502(0xfff8) == 'S' &&
			read6502(0xfff9) == 'T';
}

static void
usage()
{
	printf("\nCommander X16 Emulator r%s (%s)", VER, VER_NAME);
#ifdef GIT_REV
	printf(", "GIT_REV);
#endif
	printf("\n(C)2019, 2023 Michael Steil et al.\n");
	printf("All rights reserved. License: 2-clause BSD\n\n");
	printf("Usage: x16emu [option] ...\n\n");
	printf("-rom <rom.bin>\n");
	printf("\tOverride KERNAL/BASIC/* ROM file.\n");
	printf("-ram <ramsize>\n");
	printf("\tSpecify banked RAM size in KB (8, 16, 32, ..., 2048).\n");
	printf("\tThe default is 512.\n");
	printf("-nvram <nvram.bin>\n");
	printf("\tSpecify NVRAM image. By default, the machine starts with\n");
	printf("\tempty NVRAM and does not save it to disk.\n");
	printf("-keymap <keymap>\n");
	printf("\tEnable a specific keyboard layout decode table.\n");
	printf("-sdcard <sdcard.img>\n");
	printf("\tSpecify SD card image (partition map + FAT32)\n");
	printf("-cart <crtfile.crt>\n");
	printf("\tLoads a specially-formatted cartridge file.\n");
	printf("-cartbin <romfile.bin>\n");
	printf("\tLoads a raw cartridge file starting at ROM bank 32. After\n");
	printf("\tloading, all of the affected banks will function as RAM.\n");
	printf("-serial\n");
	printf("\tConnect host fs through Serial Bus [experimental]\n");
	printf("-nohostieee / -nohostfs\n");
	printf("\tDisable HostFS through IEEE API interception.\n");
	printf("\tIEEE API HostFS is normally enabled unless -sdcard or\n");
	printf("\t-serial is specified.\n");
	printf("-hostfsdev <unit>\n");
	printf("\tSet the HostFS IEEE device number. Range 8-31. Default: %d.\n", ieee_unit);
	printf("-fsroot <directory>\n");
	printf("\tSpecify the host filesystem directory path which is to\n");
	printf("\tact as the emulated root directory of the Commander X16.\n");
	printf("\tDefault is the current working directory.\n");
	printf("-startin <directory>\n");
	printf("\tSpecify the host filesystem directory path that the\n");
	printf("\temulated filesystem starts in. Default is the current\n");
	printf("\tworking directory if it lies within the hierarchy of fsroot,\n");
	printf("\totherwise it defaults to fsroot itself.\n");
	printf("-noemucmdkeys\n");
	printf("\tDisable emulator command keys.\n");
	printf("-capture\n");
	printf("\tStart emulator with mouse/keyboard captured.\n");
	printf("-nokeyboardcapture\n");
	printf("\tWhile in capture mode, causes the emulator not to intercept\n");
	printf("\tkeyboard combinations which are used by the operating system,\n");
	printf("\tsuch as Alt+Tab.\n");
	printf("-prg <app.prg>[,<load_addr>]\n");
	printf("\tLoad application from the *host filesystem* into RAM,\n");
	printf("\teven if an SD card is attached.\n");
	printf("\tThe override load address is hex without a prefix.\n");
	printf("-bas <app.txt>\n");
	printf("\tInject a BASIC program in ASCII encoding through the\n");
	printf("\tkeyboard.\n");
	printf("-run\n");
	printf("\tStart the -prg/-bas program using RUN\n");
	printf("-warp\n");
	printf("\tEnable warp mode, run emulator as fast as possible.\n");
	printf("-echo [{iso|raw}]\n");
	printf("\tPrint all KERNAL output to the host's stdout.\n");
	printf("\tBy default, everything but printable ASCII characters get\n");
	printf("\tescaped. \"iso\" will escape everything but non-printable\n");
	printf("\tISO-8859-15 characters and convert the output to UTF-8.\n");
	printf("\t\"raw\" will not do any substitutions.\n");
	printf("\tWith the BASIC statement \"LIST\", this can be used\n");
	printf("\tto detokenize a BASIC program.\n");
	printf("-log {K|S|V}...\n");
	printf("\tEnable logging of (K)eyboard, (S)peed, (V)ideo.\n");
	printf("\tMultiple characters are possible, e.g. -log KS\n");
	printf("-gif <file.gif>[,wait]\n");
	printf("\tRecord a gif for the video output.\n");
	printf("\tUse ,wait to start paused.\n");
	printf("\tPOKE $9FB5,2 to start recording.\n");
	printf("\tPOKE $9FB5,1 to capture a single frame.\n");
	printf("\tPOKE $9FB5,0 to pause.\n");
	printf("-wav <file.wav>[{,wait|,auto}]\n");
	printf("\tRecord a wav for the audio output.\n");
	printf("\tUse ,wait to start paused, or ,auto to start paused and automatically begin recording on the first non-zero audio signal.\n");
	printf("\tPOKE $9FB6,2 to automatically begin recording on the first non-zero audio signal.\n");
	printf("\tPOKE $9FB6,1 to begin recording immediately.\n");
	printf("\tPOKE $9FB6,0 to pause.\n");
	printf("-scale {1|2|3|4}\n");
	printf("\tScale output to an integer multiple of 640x480\n");
	printf("-quality {nearest|linear|best}\n");
	printf("\tScaling algorithm quality\n");
	printf("-widescreen\n");
	printf("\tStretch output to 16:9 resolution to mimic display of a widescreen monitor.\n");
	printf("-fullscreen\n");
	printf("\tStart up in fullscreen mode instead of in a window.\n");
	printf("-opacity (0.0,...,1.0)\n");
	printf("\tSet the opacity value (0.0 for transparent, 1.0 for opaque) of the window. (default: %.1f)\n", window_opacity);
	printf("-debug [<address>]\n");
	printf("\tEnable debugger. Optionally, set a breakpoint\n");
	printf("-randram\n");
	printf("\t(deprecated, no effect)\n");
	printf("-zeroram\n");
	printf("\tSet all RAM to zero instead of uninitialized random values\n");
	printf("-wuninit\n");
	printf("\tPrints warning to stdout if uninitialized RAM is accessed\n");
	printf("-dump {C|R|B|V}...\n");
	printf("\tConfigure system dump: (C)PU, (R)AM, (B)anked-RAM, (V)RAM\n");
	printf("\tMultiple characters are possible, e.g. -dump CV ; Default: RB\n");
	printf("-joy1\n");
	printf("\tEnable binding a gamepad to SNES controller port 1\n");
	printf("-joy2\n");
	printf("\tEnable binding a gamepad to SNES controller port 2\n");
	printf("-joy3\n");
	printf("\tEnable binding a gamepad to SNES controller port 3\n");
	printf("-joy4\n");
	printf("\tEnable binding a gamepad to SNES controller port 4\n");
	printf("-sound <output device>\n");
	printf("\tSet the output device used for audio emulation\n");
	printf("\tIf output device is 'none', no audio is generated\n");
	printf("-abufs <number of audio buffers>\n");
	printf("\tSet the number of audio buffers used for playback.\n");
	printf("\tIf using HostFS, the default is 32, otherwise 8.\n");
	printf("\tIncreasing this will reduce stutter on slower computers,\n");
	printf("\tbut will increase audio latency.\n");
	printf("-rtc\n");
	printf("\tSet the real-time-clock to the current system time and date.\n");
	printf("-via2\n");
	printf("\tInstall the second VIA chip expansion at $9F10\n");
	printf("-testbench\n");
	printf("\tHeadless mode for unit testing with an external test runner\n");
	printf("-mhz <integer>\n");
	printf("\tRun the emulator with a system clock speed other than the default of\n");
	printf("\t8 MHz. Valid values are in the range of 1-40, inclusive. This option\n");
	printf("\tis meant mainly for benchmarking, and may not reflect accurate\n");
	printf("\thardware behavior.\n");
	printf("-midline-effects\n");
	printf("\tApproximate mid-line raster effects when changing tile, sprite,\n");
	printf("\tand palette data. Requires a fast host CPU.\n");
	printf("-enable-ym2151-irq\n");
	printf("\tConnect the YM2151 IRQ source to the emulated CPU. This option increases\n");
	printf("\tCPU usage as audio render is triggered for every CPU instruction.\n");
#ifdef TRACE
	printf("-trace [<address>]\n");
	printf("\tPrint instruction trace. Optionally, a trigger address\n");
	printf("\tcan be specified.\n");
#endif
	printf("-version\n");
	printf("\tPrint additional version information of the emulator and ROM.\n");
	printf("\n");
	exit(1);
}

void
usage_keymap()
{
	printf("The following keymaps are supported:\n");
	for (int i = 0; i < sizeof(keymaps)/sizeof(*keymaps); i++) {
		printf("\t%s\n", keymaps[i]);
	}
	exit(1);
}

int
main(int argc, char **argv)
{
	char *rom_filename = "rom.bin";
	char rom_path_data[PATH_MAX];

	char *rom_path = rom_path_data;
	char *prg_path = NULL;
	char *bas_path = NULL;
	char *sdcard_path = NULL;
	bool run_test = false;
	int test_number = 0;
	int audio_buffers = 8;
	bool zeroram = false;
	bool audio_buffers_set = false;
	bool hostfs_set = false;

	const char *audio_dev_name = NULL;

	run_after_load = false;

	char *base_path = SDL_GetBasePath();

	// This causes the emulator to load ROM data from the executable's directory when
	// no ROM file is specified on the command line.
	memcpy(rom_path, base_path, strlen(base_path) + 1);
	strncpy(rom_path + strlen(rom_path), rom_filename, PATH_MAX - strlen(rom_path));
	memory_randomize_ram(true);

	argc--;
	argv++;

	while (argc > 0) {
		if (!strcmp(argv[0], "-rom")) {
			argc--;
			argv++;
			if (!argc || argv[0][0] == '-') {
				usage();
			}
			rom_path = argv[0];
			argc--;
			argv++;
		} else if (!strcmp(argv[0], "-ram")) {
			argc--;
			argv++;
			if (!argc || argv[0][0] == '-') {
				usage();
			}
			int kb = atoi(argv[0]);
			if (!((kb & 7)==0) || kb < 8 || kb > 2048) {
				printf("-ram value must be a multiple of 8 in the range of 8-2048.\n");
				exit(1);
			}
			num_ram_banks = kb /8;
			argc--;
			argv++;
		} else if (!strcmp(argv[0], "-keymap")) {
			argc--;
			argv++;
			if (!argc || argv[0][0] == '-') {
				usage_keymap();
			}
			bool found = false;
			for (int i = 0; i < sizeof(keymaps)/sizeof(*keymaps); i++) {
				if (!strcmp(argv[0], keymaps[i])) {
					found = true;
					keymap = i;
				}
			}
			if (!found) {
				usage_keymap();
			}
			argc--;
			argv++;
		} else if (!strcmp(argv[0], "-prg")) {
			argc--;
			argv++;
			if (!argc || argv[0][0] == '-') {
				usage();
			}
			prg_path = argv[0];
			argc--;
			argv++;
		} else if (!strcmp(argv[0], "-run")) {
			argc--;
			argv++;
			run_after_load = true;
		} else if (!strcmp(argv[0], "-bas")) {
			argc--;
			argv++;
			if (!argc || argv[0][0] == '-') {
				usage();
			}
			bas_path = argv[0];
			argc--;
			argv++;
		} else if (!strcmp(argv[0], "-test")) {
			argc--;
			argv++;
			if (!argc || argv[0][0] == '-') {
				usage();
			}
			test_number = atoi(argv[0]);
			run_test = true;
			argc--;
			argv++;
		} else if (!strcmp(argv[0], "-nvram")) {
			argc--;
			argv++;
			if (!argc || argv[0][0] == '-') {
				usage();
			}
			nvram_path = argv[0];
			argc--;
			argv++;
		} else if (!strcmp(argv[0], "-sdcard")) {
			argc--;
			argv++;
			if (!argc || argv[0][0] == '-') {
				usage();
			}
			sdcard_path = argv[0];
			argc--;
			argv++;
		} else if (!strcmp(argv[0], "-cart")) {
			argc--;
			argv++;
			if (!argc || argv[0][0] == '-') {
				usage();
			}
			cartridge_path = argv[0];
			argc--;
			argv++;
		} else if (!strcmp(argv[0], "-cartbin")) {
			argc--;
			argv++;
			if (!argc || argv[0][0] == '-') {
				usage();
			}
			cartridge_new();
			cartridge_define_bank_range(32, 255, CART_BANK_UNINITIALIZED_RAM);
			cartridge_import_files(argv, 1, 32, CART_BANK_INITIALIZED_RAM, 0);
			argc--;
			argv++;
		} else if (!strcmp(argv[0], "-warp")) {
			argc--;
			argv++;
			warp_mode = true;
		} else if (!strcmp(argv[0], "-echo")) {
			argc--;
			argv++;
			if (argc && argv[0][0] != '-') {
				if (!strcmp(argv[0], "raw")) {
					echo_mode = ECHO_MODE_RAW;
				} else if (!strcmp(argv[0], "iso")) {
						echo_mode = ECHO_MODE_ISO;
				} else {
					usage();
				}
				argc--;
				argv++;
			} else {
				echo_mode = ECHO_MODE_COOKED;
			}
		} else if (!strcmp(argv[0], "-log")) {
			argc--;
			argv++;
			if (!argc || argv[0][0] == '-') {
				usage();
			}
			for (char *p = argv[0]; *p; p++) {
				switch (tolower(*p)) {
					case 'k':
						log_keyboard = true;
						break;
					case 's':
						log_speed = true;
						break;
					case 'v':
						log_video = true;
						break;
					default:
						usage();
				}
			}
			argc--;
			argv++;
		} else if (!strcmp(argv[0], "-dump")) {
			argc--;
			argv++;
			if (!argc || argv[0][0] == '-') {
				usage();
			}
			dump_cpu = false;
			dump_ram = false;
			dump_bank = false;
			dump_vram = false;
			for (char *p = argv[0]; *p; p++) {
				switch (tolower(*p)) {
					case 'c':
						dump_cpu = true;
						break;
					case 'r':
						dump_ram = true;
						break;
					case 'b':
						dump_bank = true;
						break;
					case 'v':
						dump_vram = true;
						break;
					default:
						usage();
				}
			}
			argc--;
			argv++;
		} else if (!strcmp(argv[0], "-gif")) {
			argc--;
			argv++;
			// set up for recording
			record_gif = RECORD_GIF_PAUSED;
			if (!argc || argv[0][0] == '-') {
				usage();
			}
			gif_path = argv[0];
			argv++;
			argc--;
		} else if (!strcmp(argv[0], "-wav")) {
			argc--;
			argv++;
			// set up for recording
			if (!argc || argv[0][0] == '-') {
				usage();
			}
			wav_path = argv[0];
			argv++;
			argc--;
		} else if (!strcmp(argv[0], "-debug")) {
			argc--;
			argv++;
			debugger_enabled = true;
			if (argc && argv[0][0] != '-') {
				uint32_t bpVal = (uint32_t)strtol(argv[0], NULL, 16);
				struct breakpoint bp;
				if (bpVal < 0xA000) {
					bp.pc = bpVal;
					bp.bank = -1;
				} else {
					bp.pc = bpVal & 0xffff;
					bp.bank = bpVal >> 16;
				}
				DEBUGSetBreakPoint(bp);
				argc--;
				argv++;
			}
		} else if (!strcmp(argv[0], "-randram")) {
			/* this operation has no effect anymore, randomizing the Ram is now default */
			argc--;
			argv++;
		} else if (!strcmp(argv[0], "-zeroram")) {
			argc--;
			argv++;
			memory_randomize_ram(false);
			zeroram = true;
		} else if (!strcmp(argv[0], "-wuninit")) {
			argc--;
			argv++;
			memory_report_uninitialized_access(true);
		} else if (!strcmp(argv[0], "-joy1")) {
			argc--;
			argv++;
			Joystick_slots_enabled[0] = true;
		} else if (!strcmp(argv[0], "-joy2")){
			argc--;
			argv++;
			Joystick_slots_enabled[1] = true;
		} else if (!strcmp(argv[0], "-joy3")) {
			argc--;
			argv++;
			Joystick_slots_enabled[2] = true;
		} else if (!strcmp(argv[0], "-joy4")) {
			argc--;
			argv++;
			Joystick_slots_enabled[3] = true;
#ifdef TRACE
		} else if (!strcmp(argv[0], "-trace")) {
			argc--;
			argv++;
			if (argc && argv[0][0] != '-') {
				trace_mode = false;
				trace_address = (uint16_t)strtol(argv[0], NULL, 16);
				argc--;
				argv++;
			} else {
				trace_mode = true;
				trace_address = 0;
			}
#endif
		} else if (!strcmp(argv[0], "-scale")) {
			argc--;
			argv++;
			if(!argc || argv[0][0] == '-') {
				usage();
			}
			for(char *p = argv[0]; *p; p++) {
				switch(tolower(*p)) {
				case '1':
					window_scale = 1;
					break;
				case '2':
					window_scale = 2;
					break;
				case '3':
					window_scale = 3;
					break;
				case '4':
					window_scale = 4;
					break;
				default:
					usage();
				}
			}
			argc--;
			argv++;
		} else if (!strcmp(argv[0], "-quality")) {
			argc--;
			argv++;
			if(!argc || argv[0][0] == '-') {
				usage();
			}
			if (!strcmp(argv[0], "nearest") ||
				!strcmp(argv[0], "linear") ||
				!strcmp(argv[0], "best")) {
				scale_quality = argv[0];
			} else {
				usage();
			}
			argc--;
			argv++;
		} else if (!strcmp(argv[0], "-widescreen")) {
			argc--;
			argv++;
			screen_x_scale = 4.0/3;
		} else if (!strcmp(argv[0], "-fullscreen")) {
			argc--;
			argv++;
			fullscreen = true;
		} else if (!strcmp(argv[0], "-opacity")) {
			argc--;
			argv++;
			if (!argc || argv[0][0] == '-') {
				usage();
			}
			window_opacity = strtof(argv[0], NULL);
			argc--;
			argv++;
		} else if (!strcmp(argv[0], "-sound")) {
			argc--;
			argv++;
			if (!argc || argv[0][0] == '-') {
				audio_usage();
			}
			audio_dev_name = argv[0];
			argc--;
			argv++;
		} else if (!strcmp(argv[0], "-abufs")) {
			argc--;
			argv++;
			if (!argc || argv[0][0] == '-') {
				usage();
			}
			audio_buffers = (int)strtol(argv[0], NULL, 10);
			audio_buffers_set = true;
			argc--;
			argv++;
		} else if (!strcmp(argv[0], "-rtc")) {
			argc--;
			argv++;
			set_system_time = true;
		} else if (!strcmp(argv[0], "-serial")) {
			argc--;
			argv++;
			has_serial = true;
		} else if (!strcmp(argv[0], "-nohostieee") || !strcmp(argv[0], "-nohostfs")) {
			argc--;
			argv++;
			no_ieee_intercept = true;
			hostfs_set = false;
			using_hostfs = false;
		} else if (!strcmp(argv[0], "-hostfsdev")){
			argc--;
			argv++;
			if (!argc || argv[0][0] == '-') {
				usage();
			}
			ieee_unit = (uint8_t)strtol(argv[0], NULL, 10);
			if (ieee_unit < 8 || ieee_unit > 31) {
				usage();
			}
			hostfs_set = true;
			using_hostfs = true;
			argc--;
			argv++;
		} else if (!strcmp(argv[0], "-fsroot")) {
			argc--;
			argv++;
			if (!argc || argv[0][0] == '-') {
				usage();
			}
			fsroot_path = (uint8_t *)argv[0];
			argc--;
			argv++;
		} else if (!strcmp(argv[0], "-startin")) {
			argc--;
			argv++;
			if (!argc || argv[0][0] == '-') {
				usage();
			}
			startin_path = (uint8_t *)argv[0];
			argc--;
			argv++;
		} else if (!strcmp(argv[0], "-noemucmdkeys")) {
			argc--;
			argv++;
			disable_emu_cmd_keys = true;
		} else if (!strcmp(argv[0], "-capture")) {
			argc--;
			argv++;
			grab_mouse = true;
		} else if (!strcmp(argv[0], "-nokeyboardcapture")) {
			argc--;
			argv++;
			no_keyboard_capture = true;
		} else if (!strcmp(argv[0], "-via2")) {
			argc--;
			argv++;
			has_via2 = true;
		} else if (!strcmp(argv[0], "-version")){
			printf("%s", VER_INFO);
#ifdef GIT_REV
			printf(" "GIT_REV"\n");
#else
			printf("\n");
#endif
			argc--;
			argv++;
			exit(0);
		} else if (!strcmp(argv[0], "-testbench")){
			printf("Testbench mode...\n");
			fflush(stdout);
			argc--;
			argv++;
			testbench=true;
			headless=true;
		} else if (!strcmp(argv[0], "-mhz")){
			argc--;
			argv++;
			if (!argc || argv[0][0] == '-') {
				usage();
			}
			MHZ = (uint8_t)strtol(argv[0], NULL, 10);
			if (MHZ < 1 || MHZ > 40) {
				usage();
			}
			argc--;
			argv++;
		} else if (!strcmp(argv[0], "-midline-effects")){
			argc--;
			argv++;
			enable_midline = true;
		} else if (!strcmp(argv[0], "-enable-ym2151-irq")){
			argc--;
			argv++;
			ym2151_irq_support = true;
		} else {
			usage();
		}
	}

	SDL_RWops *f = SDL_RWFromFile(rom_path, "rb");
	if (!f) {
		printf("Cannot open %s!\n", rom_path);
		exit(1);
	}
	size_t rom_size = SDL_RWread(f, ROM, ROM_SIZE, 1);
	(void)rom_size;
	SDL_RWclose(f);

	if (nvram_path) {
		SDL_RWops *f = SDL_RWFromFile(nvram_path, "rb");
		if (f) {
			SDL_RWread(f, nvram, 1, sizeof(nvram));
			SDL_RWclose(f);
		}
	}

	if (sdcard_path) {
		sdcard_set_path(sdcard_path);
		if (!hostfs_set) {
			using_hostfs = false;
		}
	}

	if (using_hostfs && !audio_buffers_set) {
#ifdef __EMSCRIPTEN__
		audio_buffers = 8; // wasm has larger buffers in audio.c, so we keep it 8 even w/ HostFS
#else
		audio_buffers = 32;
#endif
	}

	if (cartridge_path) {
		if (!cartridge_load(cartridge_path, !zeroram)) {
			printf("Cannot open %s!\n", cartridge_path);
			exit(1);
		}
	}

	prg_override_start = -1;
	if (prg_path) {
		char *comma = strchr(prg_path, ',');
		if (comma) {
			prg_override_start = (uint16_t)strtol(comma + 1, NULL, 16);
			*comma = 0;
		}

		prg_file = SDL_RWFromFile(prg_path, "rb");
		if (!prg_file) {
			printf("Cannot open %s!\n", prg_path);
			exit(1);
		}
	}

	if (bas_path) {
		SDL_RWops *bas_file = SDL_RWFromFile(bas_path, "r");
		if (!bas_file) {
			printf("Cannot open %s!\n", bas_path);
			exit(1);
		}
		paste_text = paste_text_data;
		size_t paste_size = SDL_RWread(bas_file, paste_text, 1, sizeof(paste_text_data) - 1);
		if (run_after_load) {
			strncpy(paste_text + paste_size, "\rRUN\r", sizeof(paste_text_data) - paste_size);
		} else {
			paste_text[paste_size] = 0;
		}
		SDL_RWclose(bas_file);
	}

	if (run_test) {
		paste_text = paste_text_data;
		snprintf(paste_text, sizeof(paste_text_data), "TEST %d\r", test_number);
	}

#ifdef SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR
	// Don't disable compositing (on KDE for example)
	// Available since SDL 2.0.8
	SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
#endif
#ifdef __EMSCRIPTEN__
	emscripten_set_main_loop(emscripten_main_loop, 0, 0);
#endif
	if (!headless) {
		SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO | SDL_INIT_TIMER);
		audio_init(audio_dev_name, audio_buffers);
		video_init(window_scale, screen_x_scale, scale_quality, fullscreen, window_opacity);
	}

	wav_recorder_set_path(wav_path);

	memory_init();

	joystick_init();

	rtc_init(set_system_time);

	machine_reset();

	timing_init();

	instruction_counter = 0;

#ifdef __EMSCRIPTEN__
	emscripten_cancel_main_loop();
	emscripten_set_main_loop(emscripten_main_loop, 0, 1);
#else
	emulator_loop(NULL);
#endif

	main_shutdown();
	return 0;
}

void main_shutdown() {
	if (!headless){
		wav_recorder_shutdown();
		audio_close();
		video_end();
		SDL_Quit();
	}
	if(cartridge_path) {
		cartridge_save_nvram();
		cartridge_unload();
	}
	files_shutdown();

#ifdef PERFSTAT
	for (int pc = 0xc000; pc < sizeof(stat)/sizeof(*stat); pc++) {
		if (stat[pc] == 0) {
			continue;
		}
		char *label = label_for_address(pc);
		if (!label) {
			continue;
		}
		char *original_label = label;
		uint16_t pc2 = pc;
		if (label[0] == '@') {
			label = NULL;
			while (!label || label[0] == '@') {
				pc2--;
				label = label_for_address(pc2);
			}
		}
		printf("%d\t $%04X %s+%d", stat[pc], pc, label, pc-pc2);
		if (pc-pc2 != 0) {
			printf(" (%s)", original_label);
		}
		printf("\n");
	}
#endif

}

bool
set_kernal_status(uint8_t s)
{
	// There is no KERNAL API to write the STATUS variable.
	// But there is code to read it, READST, which should
	// always look like this:
	// 00:.,d6a0 ad 89 02 lda $0289
	// 00:.,d6a3 0d 89 02 ora $0289
	// 00:.,d6a6 8d 89 02 sta $0289
	// We can extract the location of the STATUS variable
	// from it.

	// JMP in the KERNAL API vectors
	if (read6502(0xffb7) != 0x4c) {
		return false;
	}
	// target of KERNAL API vector JMP
	uint16_t readst = read6502(0xffb8) | read6502(0xffb9) << 8;
	if (readst < 0xc000) {
		return false;
	}
	// ad 89 02 lda $0289
	if (read6502(readst) != 0xad) {
		return false;
	}
	// ad 89 02 lda $0289
	if (read6502(readst + 3) != 0x0d) {
		return false;
	}
	// ad 89 02 lda $0289
	if (read6502(readst + 6) != 0x8d) {
		return false;
	}
	uint16_t status0 = read6502(readst+1) | read6502(readst+2) << 8;
	uint16_t status1 = read6502(readst+4) | read6502(readst+5) << 8;
	uint16_t status2 = read6502(readst+7) | read6502(readst+8) << 8;
	// all three addresses must be the same
	if (status0 != status1 || status0 != status2) {
		return false;
	}

	// everything okay, write the status!
	RAM[status0] = s;
	return true;
}

bool
handle_ieee_intercept()
{
	if (no_ieee_intercept) {
		return false;
	}

	if (pc < 0xFEB1 || !is_kernal()) {
		return false;
	}

	if (has_serial) {
		// if we do bit-level serial bus emulation, we don't
		// do high-level KERNAL IEEE API interception
		return false;
	}
	
	if (sdcard_attached && !prg_file && !using_hostfs) {
		// if should emulate an SD card (and don't need to
		// hack a PRG into RAM), we skip HostFS if it uses unit 8
		return false;
	}

	if (sdcard_attached && prg_file && prg_finished_loading && !using_hostfs) {
		// also skip if we should do SD card and we're done
		// with the PRG hack if HostFS uses unit 8
		return false;
	}

	uint64_t base_ticks = SDL_GetPerformanceCounter();

	static int count_unlistn = 0;
	bool handled = true;
	int s = -1;
	switch(pc) {
		case 0xFEB1: {
			uint16_t count = a;
			s=MCIOUT(y << 8 | x, &count, status & 0x01);
			if (s == -2) {
				handled = false;
			} else if (s == -3) {
				status = (status | 1); // SEC (unsupported, or in this case, no open context)
			} else {
				x = count & 0xff;
				y = count >> 8;
				status &= 0xfe; // clear C -> supported
			}
			break;
		}
		case 0xFF44: {
			uint16_t count = a;
			s=MACPTR(y << 8 | x, &count, status & 0x01);
			if (s == -2) {
				handled = false;
			} else if (s == -3) {
				status = (status | 1); // SEC (unsupported, or in this case, no open context)
			} else {
				x = count & 0xff;
				y = count >> 8;
				status &= 0xfe; // clear C -> supported
			}
			break;
		}
		case 0xFF93:
			s=SECOND(a);
			if (s == -2) {
				handled = false;
			}
			break;
		case 0xFF96:
			s=TKSA(a);
			if (s == -2) {
				handled = false;
			}
			break;
		case 0xFFA5:
			s=ACPTR(&a);
			if (s == -2) {
				handled = false;
			} else {
				status = (status & ~3) | (!a << 1); // unconditional CLC, and set zero flag based on byte read
			}
			break;
		case 0xFFA8:
			s=CIOUT(a);
			if (s == -2) {
				handled = false;
			} else {
				status = (status & ~1); // unconditonal CLC
			}
			break;
		case 0xFFAB:
			s=UNTLK();
			if (s == -2) {
				handled = false;
			}
			break;
		case 0xFFAE:
			s=UNLSN();
			if (s == -2) {
				handled = false;
			}
			if (prg_file && sdcard_path_is_set() && ++count_unlistn == 4) {
				// after auto-loading a PRG from the host fs,
				// switch to the SD card if requested
				// 4x UNLISTEN:
				//    2x for LOAD"AUTOBOOT.X16*"
				//    2x for LOAD":*"
				prg_finished_loading = true;
				sdcard_attach();
			}
			break;
		case 0xFFB1:
			s=LISTEN(a);
			if (s == -2) {
				handled = false;
			} else {
				status = (status & ~1); // unconditonal CLC
			}
			break;
		case 0xFFB4:
			s=TALK(a);
			if (s == -2) {
				handled = false;
			} else {
				status = (status & ~1); // unconditonal CLC
			}
			break;
		default:
			handled = false;
			break;
	}

	if (handled) {
		// Add the number CPU cycles equivalent to the amount of time that the operation actually took
		// to prevent the emu from warping after a hostfs load
		uint64_t perf_diff = SDL_GetPerformanceCounter() - base_ticks;
		uint32_t missed_ticks = (uint64_t)(perf_diff * 1000000ULL * MHZ) / SDL_GetPerformanceFrequency();
		clockticks6502 += missed_ticks;
		if (s >= 0) {
			if (!set_kernal_status(s)) {
				printf("Warning: Could not set STATUS!\n");
			}
		}

		pc = (RAM[0x100 + sp + 1] | (RAM[0x100 + sp + 2] << 8)) + 1;
		sp += 2;
	}
	return handled;
}

void
emscripten_main_loop(void) {
	emulator_loop(NULL);
}


void *
emulator_loop(void *param)
{
	uint32_t old_clockticks6502 = clockticks6502;
	for (;;) {
		if (smc_requested_reset) machine_reset();

		if (testbench && pc == 0xfffd){
			testbench_init();
		}

		if (debugger_enabled) {
			int dbgCmd = DEBUGGetCurrentStatus();
			if (dbgCmd > 0) continue;
			if (dbgCmd < 0) break;
		}

#ifdef PERFSTAT

//		if (memory_get_rom_bank() == 3) {
//			stat[pc]++;
//		}
		if (memory_get_rom_bank() == 3) {
			static uint8_t old_sp;
			static uint16_t base_pc;
			if (sp < old_sp) {
				base_pc = pc;
			}
			old_sp = sp;
			stat[base_pc]++;
		}
#endif

#ifdef TRACE
		if (pc == trace_address && trace_address != 0) {
			trace_mode = true;
		}
		if (trace_mode) {
			char *lst = lst_for_address(pc);
			if (lst) {
				char *lf;
				while ((lf = strchr(lst, '\n'))) {
					for (int i = 0; i < 113; i++) {
						printf(" ");
					}
					for (char *c = lst; c < lf; c++) {
						printf("%c", *c);
					}
					printf("\n");
					lst = lf + 1;
				}
			}

			printf("[%8d] ", instruction_counter);

			int32_t eff_addr;

			char *label = label_for_address(pc);
			int label_len = label ? strlen(label) : 0;
			if (label) {
				printf("%s", label);
			}
			for (int i = 0; i < 20 - label_len; i++) {
				printf(" ");
			}

			if (pc >= 0xc000) {
				printf (" %02x", memory_get_rom_bank());
			} else if (pc >= 0xa000) {
				printf (" %02x", memory_get_ram_bank());
			} else {
				printf (" --");
			}

			printf(":.,%04x ", pc);

			char disasm_line[15];
			int len = disasm(pc, RAM, disasm_line, sizeof(disasm_line), false, 0, &eff_addr);
			for (int i = 0; i < len; i++) {
				printf("%02x ", read6502(pc + i));
			}
			for (int i = 0; i < 9 - 3 * len; i++) {
				printf(" ");
			}
			printf("%s", disasm_line);
			for (int i = 0; i < 15 - strlen(disasm_line); i++) {
				printf(" ");
			}

			printf("a=$%02x x=$%02x y=$%02x s=$%02x p=", a, x, y, sp);
			for (int i = 7; i >= 0; i--) {
				printf("%c", (status & (1 << i)) ? "czidb.vn"[i] : '-');
			}

			if (eff_addr == 0x9f23) {
				printf(" v=$%05x", video_get_address(0));
			} else if (eff_addr == 0x9f24) {
				printf(" v=$%05x", video_get_address(1));
			} else if (eff_addr >= 0) {
				printf(" m=$%04x ", eff_addr);
			} else {
				printf("         ");
			}

			if (lst) {
				printf("    %s", lst);
			}

			printf("\n");
		}
#endif

		if (handle_ieee_intercept()) {
			continue;
		}

		step6502();
		uint32_t clocks = clockticks6502 - old_clockticks6502;
		old_clockticks6502 = clockticks6502;
		bool new_frame = false;
		via1_step(clocks);
		vera_spi_step(clocks);
		if (has_serial) {
			serial_step(clocks);
		}
		if (has_via2) {
			via2_step(clocks);
		}
		if (!headless) {
			new_frame |= video_step(MHZ, clocks, false);
		}

		for (uint32_t i = 0; i < clocks; i++) {
			i2c_step();
		}
		rtc_step(clocks);

		if (!headless) {
			audio_step(clocks);
		}

		instruction_counter++;

		if (!headless && new_frame) {
			if (nvram_dirty && nvram_path) {
				SDL_RWops *f = SDL_RWFromFile(nvram_path, "wb");
				if (f) {
					SDL_RWwrite(f, nvram, 1, sizeof(nvram));
					SDL_RWclose(f);
				}
				nvram_dirty = false;
			}

			if (!video_update()) {
				break;
			}

			timing_update();
#ifdef __EMSCRIPTEN__
			// After completing a frame we yield back control to the browser to stay responsive
			return 0;
#endif
		}

		// The optimization from the opportunistic batching of audio rendering 
		// is lost if we need to track the YM2151 IRQ, so it has been made a
		// command-line switch that's disabled by default.
		if (ym2151_irq_support) {
			audio_render();
		}

		if (video_get_irq_out() || via1_irq() || (has_via2 && via2_irq()) || (ym2151_irq_support && YM_irq())) {
//			printf("IRQ!\n");
			irq6502();
		}

		if (pc == 0xffff) {
			if (save_on_exit) {
				machine_dump("CPU program counter reached $ffff");
			}
			break;
		}

		// Change this comparison value if ever additional KERNAL
		// API calls are snooped in this routine.

		if (pc >= 0xff68 && is_kernal()) {
			if (pc == 0xff68) {
				kernal_mouse_enabled = !!a;
				SDL_ShowCursor((mouse_grabbed || kernal_mouse_enabled) ? SDL_DISABLE : SDL_ENABLE);
			}

			if (echo_mode != ECHO_MODE_NONE && pc == 0xffd2) {
				uint8_t c = a;
				if (echo_mode == ECHO_MODE_COOKED) {
					if (c == 0x0d) {
						printf("\n");
					} else if (c == 0x0a) {
						// skip
					} else if (c < 0x20 || c >= 0x80) {
						printf("\\X%02X", c);
					} else {
						printf("%c", c);
					}
				} else if (echo_mode == ECHO_MODE_ISO) {
					if (c == 0x0d) {
						printf("\n");
					} else if (c == 0x0a) {
						// skip
					} else if (c < 0x20 || (c >= 0x80 && c < 0xa0)) {
						printf("\\X%02X", c);
					} else {
						print_iso8859_15_char(c);
					}
				} else {
					printf("%c", c);
				}
				fflush(stdout);
			}

			if (pc == 0xffcf) {
				// as soon as BASIC starts reading a line...
				static bool prg_done = false;

				if (prg_file && !prg_done) {
					// LOAD":*" will cause the IEEE library
					// to load from "prg_file"
					if (prg_override_start >= 0) {
						snprintf(paste_text_data, sizeof(paste_text_data), "LOAD\":*\",%d,1,$%04X\r", ieee_unit, prg_override_start);
					} else {
						snprintf(paste_text_data, sizeof(paste_text_data), "LOAD\":*\",%d,1\r", ieee_unit);
					}
					paste_text = paste_text_data;
					prg_done = true;

					if (run_after_load) {
						if (prg_override_start >= 0) {
							snprintf(strchr(paste_text_data, 0), sizeof(paste_text_data), "SYS$%04X\r", prg_override_start);
						} else {
							snprintf(strchr(paste_text_data, 0), sizeof(paste_text_data), "RUN\r");
						}
					}
				}
				else if (testbench && !test_init_complete){
					snprintf(paste_text_data, sizeof(paste_text_data), "SYS65533\r");
					paste_text = paste_text_data;
					test_init_complete=true;
				}

				if (paste_text) {
					// ...paste BASIC code into the keyboard buffer
					pasting_bas = true;
				}
			}

		}
#if 0 // enable this for slow pasting
		if (!(instruction_counter % 100000))
#endif
		while (pasting_bas && RAM[NDX] < 10 && !(status & 0x04)) {
			uint32_t c;
			int e = 0;

			if (paste_text[0] == '\\' && paste_text[1] == 'X' && paste_text[2] && paste_text[3]) {
				uint8_t hi = strtol((char[]){paste_text[2], 0}, NULL, 16);
				uint8_t lo = strtol((char[]){paste_text[3], 0}, NULL, 16);
				c = hi << 4 | lo;
				paste_text += 4;
			} else {
				paste_text = utf8_decode(paste_text, &c, &e);
				c = iso8859_15_from_unicode(c);
			}
			if (c && !e) {
				RAM[KEYD + RAM[NDX]] = c;
				RAM[NDX]++;
			} else {
				pasting_bas = false;
				paste_text = NULL;
				if (clipboard_buffer) {
					SDL_free(clipboard_buffer);
					clipboard_buffer = NULL;
				}
			}
		}
	}

	return 0;
}
