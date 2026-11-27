// Commander X16 Emulator
// Copyright (c) 2019 Michael Steil
// All rights reserved. License: 2-clause BSD

#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <inttypes.h>
#include "glue.h"
#include "via.h"
#include "memory.h"
#include "video.h"
#include "ymglue.h"
#include "cpu/fake6502.h"
#include "wav_recorder.h"
#include "audio.h"
#include "cartridge.h"
#include "iso_8859_15.h"
#include "midi.h"

#include "extern/serialuart/serialuart_wrapper.h"
extern bool has_uart1;
extern bool has_uart2;
extern uint16_t uart1_addr;
extern uint16_t uart2_addr;
extern serialuartTL16C2550Handle uart1;
extern serialuartTL16C2550Handle uart2;

uint8_t ram_bank;
uint8_t rom_bank;

uint8_t *RAM, *BRAM;
uint8_t ROM[ROM_SIZE];
extern uint8_t *CART;

static uint8_t addr_ym = 0;

bool randomizeRAM = false;
bool reportUninitializedAccess = false;
const char *reportUsageStatisticsFilename = NULL;
bool *RAM_access_flags, *BRAM_access_flags;
uint64_t *RAM_system_reads;
uint64_t *RAM_system_writes;
uint64_t *RAM_banked_reads[256];
uint64_t *RAM_banked_writes[256];
uint64_t *ROM_banked_reads[256];
uint64_t *ROM_banked_writes[256];	// shouldn't occur for obvious reasons unless Bonk RAM is installed in a cart


static uint32_t clock_snap = 0UL;
static uint32_t clock_base = 0UL;

#define DEVICE_EMULATOR (0x9fb0)

void cpuio_write(uint8_t reg, uint8_t value);

void
memory_init()
{
	// Initialize RAM array
	RAM = calloc(RAM_SIZE, sizeof(uint8_t));
	BRAM = calloc(BRAM_SIZE, sizeof(uint8_t));

	if(reportUsageStatisticsFilename!=NULL) {
		RAM_system_reads = calloc(num_banks * BANK_SIZE, sizeof(uint64_t));
		RAM_system_writes = calloc(num_banks * BANK_SIZE, sizeof(uint64_t));
		for(int bank=0; bank<256; ++bank) {
			RAM_banked_reads[bank] = calloc(8192, sizeof(uint64_t));
			RAM_banked_writes[bank] = calloc(8192, sizeof(uint64_t));
			ROM_banked_reads[bank] = calloc(16384, sizeof(uint64_t));
			ROM_banked_writes[bank] = calloc(16384, sizeof(uint64_t));
		}
	}


	// Randomize all RAM (if option selected)
	if (randomizeRAM) {
		time_t t;
		srand((unsigned)time(&t));
		for (int i = 0; i < RAM_SIZE; i++) {
			if (i >= 0x9f00 && i < 0x10000) {
				// Leave the unused hole in the address space at 0
				// Memory dumps will likely confuse people less often
				RAM[i] = 0;
			} else {
				RAM[i] = rand();
			}
		}
		for (int i = 0; i < BRAM_SIZE; i++) {
			BRAM[i] = rand();
		}
	}

	// Initialize RAM access flag array (if option selected)
	if (reportUninitializedAccess) {
		RAM_access_flags = (bool*) malloc(RAM_SIZE * sizeof(bool));
		for (int i = 0; i < RAM_SIZE; i++) {
			RAM_access_flags[i] = false;
		}
		BRAM_access_flags = (bool*) malloc(BRAM_SIZE * sizeof(bool));
		for (int i = 0; i < BRAM_SIZE; i++) {
			BRAM_access_flags[i] = false;
		}
	}

	memory_reset();
}

void
memory_reset()
{
	// default banks are 0
	memory_set_ram_bank(0);
	memory_set_rom_bank(0);
}

void
memory_report_uninitialized_access(bool value)
{
	reportUninitializedAccess = value;
}

void
memory_report_usage_statistics(const char *filename) {
	reportUsageStatisticsFilename = filename;
}

void
memory_randomize_ram(bool value)
{
	randomizeRAM = value;
}

void
memory_initialize_cart(uint8_t *mem)
{
	if(randomizeRAM) {
		for(int i=0; i<0x4000; ++i) {
			mem[i] = rand();
		}
	} else {
		memset(mem, 0, 0x4000);
	}
}

//
// interface for fake6502
//
// if debugOn then reads memory only for debugger; no I/O, no side effects whatsoever

static const char *format_addr(uint16_t address, uint8_t bank, uint8_t x16Bank) {
	static char buffer[2 + 1 + 4 + 1];

	if (bank == 0 && address >= 0xA000) {
		if (address >= 0xA000) {
			snprintf(buffer, sizeof(buffer), "%02X:%04X", x16Bank, address);
		}
	}
	else if (is_gen2) {
		snprintf(buffer, sizeof(buffer), "%02X %04X", bank, address);
	} else {
		snprintf(buffer, sizeof(buffer), "%04X", address);
	}

	return buffer;
}

uint8_t
read6502(uint16_t address, uint8_t bank) {
	if (!is_gen2) bank = 0;
	// Report access to uninitialized RAM (if option selected)
	if (reportUninitializedAccess) {
		if (bank == 0) {
			uint8_t pc_x16_bank;

			if (opcode_addr < 0xa000) {
				pc_x16_bank = 0;
			} else if (opcode_addr < 0xc000) {
				pc_x16_bank = memory_get_ram_bank();
			} else {
				pc_x16_bank = memory_get_rom_bank();
			}

			if (address >= 0xa000 && address < 0xc000) {
				if (memory_get_ram_bank() < num_ram_banks && BRAM_access_flags[(memory_get_ram_bank() << 13) + address - 0xa000] == false){
					printf("Warning: %02X:%04X accessed uninitialized RAM address %02X:%04X\n", pc_x16_bank, opcode_addr, memory_get_ram_bank(), address);
				}
			}
		}

		if (bank != 0 || address < 0x9f00) {
			if (RAM_access_flags[bank * BANK_SIZE + address] == false) {
				printf("Warning: %s accessed uninitialized RAM address %02X %04X\n", format_addr(opcode_addr, regs.k, 0), bank, address);
			}
		}
	}

    if (reportUsageStatisticsFilename!=NULL) {
      if (bank != 0 || address < 0xa000) {
        RAM_system_reads[bank * BANK_SIZE + address]++;
      } else if (address < 0xc000) {
        RAM_banked_reads[memory_get_ram_bank()][address-0xa000]++;
      } else {
		ROM_banked_reads[rom_bank][address-0xc000]++;
      }
    }

	return real_read6502(address, bank, false, USE_CURRENT_X16_BANK);
}

uint8_t
real_read6502(uint16_t address, uint8_t bank, bool debugOn, int16_t x16Bank)
{
	if (is_gen2 && bank != 0) { // RAM
		if (bank < num_banks) {
			return RAM[bank * BANK_SIZE + address];
		} else {
			return (address >> 8) & 0xff; // open bus read
		}
	}

	if (address < 0x9f00) { // RAM
		return RAM[address];
	} else if (address < 0xa000) { // I/O
		if (!debugOn && address >= 0x9fa0) {
			// slow IO5-7 range
			clockticks6502 += 3;
		}
		if (address >= 0x9f00 && address < 0x9f10) {
			return via1_read(address & 0xf, debugOn);
		} else if (has_via2 && (address >= 0x9f10 && address < 0x9f20)) {
			return via2_read(address & 0xf, debugOn);
		} else if (address >= 0x9f20 && address < 0x9f40) {
			return video_read(address & 0x1f, debugOn);
		} else if (address >= 0x9f40 && address < 0x9f60) {
			// slow IO2 range
			if (!debugOn) {
				clockticks6502 += 3;
			}
			if ((address & 0x01) != 0) { // partial decoding in this range
				audio_render();
				return YM_read_status();
			}
			return 0x9f; // open bus read
		} else if (address >= 0x9fb0 && address < 0x9fc0) {
			// emulator state
			return emu_read(address & 0xf, debugOn);
		} else if (has_midi_card && (address & 0xfff0) == midi_card_addr) {
			// midi card
			return midi_serial_read(address & 0xf, debugOn);
		}
		//Serial UART1 (serial wifi)
		else if ( has_uart1 && (address >= uart1_addr && address <= (uart1_addr+7) ) ){
			uint8_t retVal=0;
			uart_addrread(uart1, &retVal, address & 0x07 );
			return retVal;
		}// END of Serial UART1*/
		//Serial UART2 (serial wifi)
		else if ( has_uart2 && (address >= uart2_addr && address <= (uart2_addr+7))  ){
			uint8_t retVal=0;
			uart_addrread(uart2, &retVal, address & 0x07 );
			return retVal;
		}// END of Serial UART2*/
		else {
			// future expansion
			return 0x9f; // open bus read
		}
	} else if (address < 0xc000) { // banked RAM
		int ramBank = x16Bank >= 0 ? (uint8_t)x16Bank : memory_get_ram_bank();
		if (ramBank < num_ram_banks) {
			return BRAM[(ramBank << 13) + address - 0xa000];
		} else {
			return (address >> 8) & 0xff; // open bus read
		}
	} else { // banked ROM
		int romBank = x16Bank >= 0 ? (uint8_t)x16Bank : rom_bank;
		if (romBank < 32) {
			return ROM[(romBank << 14) + address - 0xc000];
		} else {
			if (!CART) {
				return (address >> 8) & 0xff; // open bus read
			}
			return cartridge_read(address, romBank);
		}
	}
}

void
write6502(uint16_t address, uint8_t bank, uint8_t value)
{
	if (!is_gen2) bank = 0;

	if(reportUsageStatisticsFilename!=NULL) {
		if (bank != 0 || address < 0xa000) {
			RAM_system_writes[bank * BANK_SIZE + address]++;
		} else if (address < 0xc000) {
			RAM_banked_writes[memory_get_ram_bank()][address-0xa000]++;
		} else {
			// this is weird, but it does occur. And cartridges can install "Bonk RAM" in place of ROM.
			ROM_banked_writes[rom_bank][address-0xc000]++;
		}
	}

	// Update RAM access flag
	if (reportUninitializedAccess) {
		if (bank != 0 || address < 0xa000) {
			RAM_access_flags[bank * BANK_SIZE + address] = true;
		} else if (address < 0xc000) {
			if (memory_get_ram_bank() < num_ram_banks)
				BRAM_access_flags[(memory_get_ram_bank() << 13) + address - 0xa000] = true;
		}
	}

	// Write to memory
	if (is_gen2 && bank != 0) {
		if (bank < num_banks) {
			RAM[bank * BANK_SIZE + address] = value;
		}
		return;
	}

	// Write to CPU I/O ports
	if (address < 2) {
		cpuio_write(address, value);
	}
	// Write to memory
	if (address < 0x9f00) { // RAM
		RAM[address] = value;
	} else if (address < 0xa000) { // I/O
		if (address >= 0x9fa0) {
			// slow IO5-7 range
			clockticks6502 += 3;
		}
		if (address >= 0x9f00 && address < 0x9f10) {
			via1_write(address & 0xf, value);
		} else if (has_via2 && (address >= 0x9f10 && address < 0x9f20)) {
			via2_write(address & 0xf, value);
		} else if (address >= 0x9f20 && address < 0x9f40) {
			video_write(address & 0x1f, value);
		} else if (address >= 0x9f40 && address < 0x9f60) {
			// slow IO2 range
			clockticks6502 += 3;
			if ((address & 0x01) == 0) {   // YM reg (partially decoded)
				addr_ym = value;
			} else {                       // YM data (partially decoded)
				audio_render();
				YM_write_reg(addr_ym, value);
			}
		} else if (address >= 0x9fb0 && address < 0x9fc0) {
			// emulator state
			emu_write(address & 0xf, value);
		} else if (has_midi_card && (address & 0xfff0) == midi_card_addr) {
			midi_serial_write(address & 0xf, value);
		}
		else if (has_uart1 && (address >= uart1_addr && address <= (uart1_addr+7) ) ) {
			uart_addrwrite(uart1, &value, address & 0x07 );
		}
		else if (has_uart2 && (address >= uart2_addr && address <= (uart2_addr+7) ) ) {
			uart_addrwrite(uart2, &value, address & 0x07 );
		}
		//END of UART2
		else {
			// future expansion
		}
	} else if (address < 0xc000) { // banked RAM
		if (memory_get_ram_bank() < num_ram_banks) {
			BRAM[(memory_get_ram_bank() << 13) + address - 0xa000] = value;
		}
	} else { // ROM
		if (rom_bank >= 32) { // Cartridge ROM/RAM
			cartridge_write(address, rom_bank, value);
		}
		// ignore if base ROM (banks 0-31)
	}
}

void
vp6502()
{
	memory_set_rom_bank(0);
}

//
// saves the memory content into a file
//

void
memory_save(SDL_RWops *f, bool dump_ram, bool dump_bank)
{
	if (dump_ram) {
		SDL_RWwrite(f, &RAM[0], sizeof(uint8_t), is_gen2 ? num_banks * BANK_SIZE : 0xa000);
	}
	if (dump_bank) {
		SDL_RWwrite(f, &BRAM[0], sizeof(uint8_t), (num_ram_banks * 8192));
	}
}


void writestring(SDL_RWops *f, const char *string) {
	SDL_RWwrite(f, string, strlen(string), 1);
}

void memory_dump_usage_counts() {
	if(reportUsageStatisticsFilename==NULL)
		return;

	SDL_RWops *f = SDL_RWFromFile(reportUsageStatisticsFilename, "w");
	if (!f) {
		printf("Cannot write to %s!\n", reportUsageStatisticsFilename);
		return;
	}

	writestring(f, "Usage counts of all memory locations. Locations not printed have count zero.\n");
	writestring(f, "Tip: use 'sort -r -n -k 3' to sort it so it shows the most used at the top.\n");
	writestring(f, "\nsystem RAM reads:\n");
	int addr;
	char buf[100];
	for(addr=0; addr<num_banks * BANK_SIZE; ++addr) {
		if(RAM_system_reads[addr]>0) {
			SDL_RWwrite(f, buf, snprintf(buf, sizeof(buf), "r %04x %" PRIu64 "\n", addr, RAM_system_reads[addr]), 1);
		}
	}
	writestring(f, "\nsystem RAM writes:\n");
	for(addr=0; addr<65536; ++addr) {
		if(RAM_system_writes[addr]>0) {
			SDL_RWwrite(f, buf, snprintf(buf, sizeof(buf), "w %04x %" PRIu64 "\n", addr, RAM_system_writes[addr]), 1);
		}
	}
	writestring(f, "\nbanked RAM reads:\n");
	int bank;
	for(bank=0; bank<256; ++bank) {
		for(addr=0; addr<8192; ++addr) {
			if(RAM_banked_reads[bank][addr]>0) {
				SDL_RWwrite(f, buf, snprintf(buf, sizeof(buf), "r %02x:%04x %" PRIu64 "\n", bank, addr+0xa000, RAM_banked_reads[bank][addr]), 1);
			}
		}
	}
	writestring(f, "\nbanked RAM writes:\n");
	for(bank=0; bank<256; ++bank) {
		for(addr=0; addr<8192; ++addr) {
			if(RAM_banked_writes[bank][addr]>0) {
				SDL_RWwrite(f, buf, snprintf(buf, sizeof(buf), "w %02x:%04x %" PRIu64 "\n", bank, addr+0xa000, RAM_banked_writes[bank][addr]), 1);
			}
		}
	}
	writestring(f, "\nbanked ROM reads:\n");
	for(bank=0; bank<256; ++bank) {
		for(addr=0; addr<16384; ++addr) {
			if(ROM_banked_reads[bank][addr]>0) {
				SDL_RWwrite(f, buf, snprintf(buf, sizeof(buf), "r %02x:%04x %" PRIu64 "\n", bank, addr+0xc000, ROM_banked_reads[bank][addr]), 1);
			}
		}
	}
	writestring(f, "\nbanked ROM / 'Bonk RAM' writes:\n");
	for(bank=0; bank<256; ++bank) {
		for(addr=0; addr<16384; ++addr) {
			if(ROM_banked_writes[bank][addr]>0) {
				SDL_RWwrite(f, buf, snprintf(buf, sizeof(buf), "w %02x:%04x %" PRIu64 "\n", bank, addr+0xc000, ROM_banked_writes[bank][addr]), 1);
			}
		}
	}

	SDL_RWclose(f);
}


///
///
///

inline void
memory_set_ram_bank(uint8_t bank)
{
	ram_bank = bank;
}

inline uint8_t
memory_get_ram_bank()
{
	return ram_bank;
}

inline void
memory_set_rom_bank(uint8_t bank)
{
	rom_bank = bank;
}

inline uint8_t
memory_get_rom_bank()
{
	return rom_bank;
}

void
cpuio_write(uint8_t reg, uint8_t value)
{
	switch (reg) {
		case 0:
			memory_set_ram_bank(value);
			break;
		case 1:
			memory_set_rom_bank(value);
			break;
	}
}

// Control the GIF recorder
void
emu_recorder_set(gif_recorder_command_t command)
{
	// turning off while recording is enabled
	if (command == RECORD_GIF_PAUSE && record_gif != RECORD_GIF_DISABLED) {
		record_gif = RECORD_GIF_PAUSED; // need to save
	}
	// turning on continuous recording
	if (command == RECORD_GIF_RESUME && record_gif != RECORD_GIF_DISABLED) {
		record_gif = RECORD_GIF_ACTIVE;		// activate recording
	}
	// capture one frame
	if (command == RECORD_GIF_SNAP && record_gif != RECORD_GIF_DISABLED) {
		record_gif = RECORD_GIF_SINGLE;		// single-shot
	}
}

//
// read/write emulator state (feature flags)
//
// 0: debugger_enabled
// 1: log_video
// 2: log_keyboard
// 3: echo_mode
// 4: save_on_exit
// 5: record_gif
// 6: record_wav
// 7: cmd key toggle
// 8: write: reset cpu clock counter
// 8: read: snapshots cpu clock counter and reads the LSB bits 0-7
// 9: write: output debug byte 1
// 9: read: cpu clock bits 8-15
// 10: write: output debug byte 2
// 10: read: cpu clock bits 16-23
// 11: write: write character to STDOUT of console
// 11: read: cpu clock MSB bits 24-31
// POKE $9FB3,1:PRINT"ECHO MODE IS ON":POKE $9FB3,0
void
emu_write(uint8_t reg, uint8_t value)
{
	bool v = value != 0;
	switch (reg) {
		case 0: debugger_enabled = v; break;
		case 1: log_video = v; break;
		case 2: log_keyboard = v; break;
		case 3: echo_mode = value; break;
		case 4: save_on_exit = v; break;
		case 5: emu_recorder_set((gif_recorder_command_t) value); break;
		case 6: wav_recorder_set((wav_recorder_command_t) value); break;
		case 7: disable_emu_cmd_keys = v; break;
		case 8: clock_base = clockticks6502; break;
		case 9: printf("User debug 1: $%02x\n", value); fflush(stdout); break;
		case 10: printf("User debug 2: $%02x\n", value); fflush(stdout); break;
		case 11: {
			if (value == 0x09 || value == 0x0a || value == 0x0d || (value >= 0x20 && value < 0x7f)) {
				printf("%c", value);
			} else if (value >= 0xa1) {
				print_iso8859_15_char((char) value);
			} else {
				printf("\xef\xbf\xbd"); // �
			}
			fflush(stdout);
			break;
		}
		default: printf("WARN: Invalid register %x\n", DEVICE_EMULATOR + reg);
	}
}

uint8_t
emu_read(uint8_t reg, bool debugOn)
{
	if (reg == 0) {
		return debugger_enabled ? 1 : 0;
	} else if (reg == 1) {
		return log_video ? 1 : 0;
	} else if (reg == 2) {
		return log_keyboard ? 1 : 0;
	} else if (reg == 3) {
		return echo_mode;
	} else if (reg == 4) {
		return save_on_exit ? 1 : 0;
	} else if (reg == 5) {
		return record_gif;
	} else if (reg == 6) {
		return wav_recorder_get_state();
	} else if (reg == 7) {
		return disable_emu_cmd_keys ? 1 : 0;

	} else if (reg == 8) {
		if (!debugOn)
			clock_snap = clockticks6502 - clock_base;
		return (clock_snap >> 0) & 0xff;
	} else if (reg == 9) {
		return (clock_snap >> 8) & 0xff;
	} else if (reg == 10) {
		return (clock_snap >> 16) & 0xff;
	} else if (reg == 11) {
		return (clock_snap >> 24) & 0xff;

	} else if (reg == 13) {
		return keymap;
	} else if (reg == 14) {
		return '1'; // emulator detection
	} else if (reg == 15) {
		return '6'; // emulator detection
	}
	if (!debugOn) printf("WARN: Invalid register %x\n", DEVICE_EMULATOR + reg);
	return -1;
}
