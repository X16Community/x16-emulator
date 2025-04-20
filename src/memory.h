// Commander X16 Emulator
// Copyright (c) 2019 Michael Steil
// All rights reserved. License: 2-clause BSD

#ifndef _MEMORY_H_
#define _MEMORY_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <SDL.h>

#define BANK_SIZE 65536

#define USE_CURRENT_X16_BANK (-1)
#define debug_read6502(a, b, x) real_read6502((a), (b), true, (x))

// X16 r0-r15
#define X16_REG_R0L (direct_page_add(2))
#define X16_REG_R0H (direct_page_add(3))
#define X16_REG_R1L (direct_page_add(4))
#define X16_REG_R1H (direct_page_add(5))
#define X16_REG_R2L (direct_page_add(6))
#define X16_REG_R2H (direct_page_add(7))
#define X16_REG_R3L (direct_page_add(8))
#define X16_REG_R3H (direct_page_add(9))
#define X16_REG_R4L (direct_page_add(10))
#define X16_REG_R4H (direct_page_add(11))
#define X16_REG_R5L (direct_page_add(12))
#define X16_REG_R5H (direct_page_add(13))
#define X16_REG_R6L (direct_page_add(14))
#define X16_REG_R6H (direct_page_add(15))
#define X16_REG_R7L (direct_page_add(16))
#define X16_REG_R7H (direct_page_add(17))
#define X16_REG_R8L (direct_page_add(18))
#define X16_REG_R8H (direct_page_add(19))
#define X16_REG_R9L (direct_page_add(20))
#define X16_REG_R9H (direct_page_add(21))
#define X16_REG_R10L (direct_page_add(22))
#define X16_REG_R10H (direct_page_add(23))
#define X16_REG_R11L (direct_page_add(24))
#define X16_REG_R11H (direct_page_add(25))
#define X16_REG_R12L (direct_page_add(26))
#define X16_REG_R12H (direct_page_add(27))
#define X16_REG_R13L (direct_page_add(28))
#define X16_REG_R13H (direct_page_add(29))
#define X16_REG_R14L (direct_page_add(30))
#define X16_REG_R14H (direct_page_add(31))
#define X16_REG_R15L (direct_page_add(32))
#define X16_REG_R15H (direct_page_add(33))

uint8_t read6502(uint16_t address, uint8_t bank);
uint8_t real_read6502(uint16_t address, uint8_t bank, bool debugOn, int16_t x16Bank);
void write6502(uint16_t address, uint8_t bank, uint8_t value);
void vp6502();

void memory_init();
void memory_reset();
void memory_report_uninitialized_access(bool);
void memory_report_usage_statistics(const char *filename);
void memory_randomize_ram(bool);

void memory_save(SDL_RWops *f, bool dump_ram, bool dump_bank);
void memory_dump_usage_counts();

void memory_set_ram_bank(uint8_t bank);
void memory_set_rom_bank(uint8_t bank);

uint8_t memory_get_ram_bank();
uint8_t memory_get_rom_bank();

uint8_t emu_read(uint8_t reg, bool debugOn);
void emu_write(uint8_t reg, uint8_t value);

#endif
