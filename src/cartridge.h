// Commander X16 Emulator
// Copyright (c) 2023 Stephen Horn
// All rights reserved. License: 2-clause BSD

#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#define CART_MAX_BANKS 224
#define CART_BANK_SIZE 0x4000
#define CART_MAX_SIZE (CART_BANK_SIZE * CART_MAX_BANKS)

#define CART_MAGIC_NUMBER_SIZE 16
#define CART_VERSION_SIZE 16
#define CART_DESCRIPTION_SIZE 32
#define CART_AUTHOR_SIZE 32
#define CART_COPYRIGHT_SIZE 32
#define CART_PROGRAM_VERSION_SIZE 32
#define CART_RESERVED_SIZE (64 + 32)

#define CART_BANK_NONE 0
#define CART_BANK_ROM 1
#define CART_BANK_UNINITIALIZED_RAM 2
#define CART_BANK_INITIALIZED_RAM 3
#define CART_BANK_UNINITIALIZED_NVRAM 4
#define CART_BANK_INITIALIZED_NVRAM 5
#define CART_NUM_BANK_TYPES 5

struct x16cartridge_header
{
	char magic_number[CART_MAGIC_NUMBER_SIZE];
	char cart_version[CART_VERSION_SIZE];
	char description[CART_DESCRIPTION_SIZE];
	char author[CART_AUTHOR_SIZE];
	char copyright[CART_COPYRIGHT_SIZE];
	char prg_version[CART_PROGRAM_VERSION_SIZE];
	char reserved[CART_RESERVED_SIZE];
	uint8_t bank_info[CART_MAX_BANKS];
};

struct cartridge_import;

bool cartridge_load(const char *cartridge_file, bool randomize);
void cartridge_unload();
void cartridge_new();

void cartridge_set_desc(const char *name);
void cartridge_set_author(const char *name);
void cartridge_set_copyright(const char *name);
void cartridge_set_program_version(const char *name);

void cartridge_get_desc(char *buffer, size_t buffer_size);
void cartridge_get_author(char *buffer, size_t buffer_size);
void cartridge_get_copyright(char *buffer, size_t buffer_size);
void cartridge_get_program_version(char *buffer, size_t buffer_size);

bool cartridge_define_bank_range(uint8_t start_bank, uint8_t end_bank, uint8_t bank_type);
bool cartridge_import_files(char **bin_files, int num_files, uint8_t start_bank, uint8_t bank_type, uint8_t fill_value);
bool cartridge_fill(uint8_t start_bank, uint8_t end_bank, uint8_t bank_type, uint8_t fill_value);

bool cartridge_save(const char *cartridge_file);
bool cartridge_save_nvram();

uint8_t cartridge_read(uint16_t address, uint8_t bank);
void cartridge_write(uint16_t address, uint8_t bank, uint8_t value);
uint8_t cartridge_get_bank_type(uint8_t bank);
