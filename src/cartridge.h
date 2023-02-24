// Commander X16 Emulator
// Copyright (c) 2023 Stephen Horn
// All rights reserved. License: 2-clause BSD

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define CART_MAX_BANKS 224
#define CART_BANK_SIZE 0x4000
#define CART_DESCRIPTION_SIZE 28

#define CART_BANK_NONE 0
#define CART_BANK_ROM 1
#define CART_BANK_RAM 2
#define CART_BANK_INITIALIZED_NVRAM 3
#define CART_BANK_UNINITIALIZED_NVRAM 4

struct x16cartridge_header
{
	uint8_t magic_number[2];
	uint8_t major_version;
	uint8_t minor_version;
	char description[CART_DESCRIPTION_SIZE];
	uint8_t bank_info[CART_MAX_BANKS];
};

struct cartridge_import;

bool cartridge_load(const char *cartridge_file);
void cartridge_new();
void cartridge_set_desc(const char *name);
void cartridge_define_banks(uint8_t start_bank, uint8_t num_banks, uint8_t bank_type);
void cartridge_define_bank_range(uint8_t start_bank, uint8_t end_bank, uint8_t bank_type);
bool cartridge_import_files(const char **bin_files, int num_files, uint8_t start_bank, uint8_t bank_type);

bool cartridge_save(const char *cartridge_file);

uint8_t cartridge_read(uint16_t address, uint8_t bank);
void cartridge_write(uint16_t address, uint8_t bank, uint8_t value);
