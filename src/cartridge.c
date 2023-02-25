#include "cartridge.h"
#include "files.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

struct x16cartridge_header Cartridge_info;
uint8_t *CART = NULL;
char *Cartridge_path = NULL;
char *Cartridge_nvram_path = NULL;

const char Cartridge_magic_number[CART_MAGIC_NUMBER_SIZE] = { 'C', 'X', '1', '6', ' ', 'C', 'A', 'R', 'T', 'R', 'I', 'D', 'G', 'E', '\r', '\n' };
const char Cartridge_current_version[CART_VERSION_SIZE]   = { '0', '1', '.', '0', '0', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};

static const char *
find_extension(const char *path, const char *mark)
{
	if(path == NULL) {
		return NULL;
	}

	if(mark == NULL) {
		mark = path + strlen(path);
	}

	while(mark > path) {
		if(*mark == '.') {
			return mark;
		}
		--mark;
	}

	return NULL;
}

static void 
initialize_cart_bank(uint8_t *mem, bool randomize)
{
	if(randomize) {
		for(int i=0; i<CART_BANK_SIZE; ++i) {
			mem[i] = rand();
		}
	} else {
		memset(mem, 0, CART_BANK_SIZE);
	}
}

bool 
cartridge_load(const char *path, bool randomize)
{
	const char *extension = find_extension(path, NULL);
	if(strcmp(extension, ".gz") == 0) {
		extension = find_extension(path, extension-1);
	}

	if(strncmp(extension, ".crt", 4) != 0) {
		printf("Path \"%s\" does not appear to be a cartridge (.crt) file.\n", path);
		return false;
	}

	const size_t path_len = strlen(path);
	Cartridge_path = malloc(path_len + 1);
	strcpy(Cartridge_path, path);

	Cartridge_nvram_path = malloc(path_len + 3);
	strcpy(Cartridge_nvram_path, Cartridge_path);
	strcpy(Cartridge_nvram_path + path_len - 3, "nvram");

	struct x16file *cart = x16open(Cartridge_path, "rb");
	struct x16file *nvram = x16open(Cartridge_nvram_path, "rb");

	bool ok = !!x16read(cart, (void *)(&Cartridge_info), sizeof(struct x16cartridge_header), 1);
	if(!ok) {
		printf("Could not load cartridge \"%s\": Could not read header.\n", path);\
		goto load_end;
	}

	if(memcmp(Cartridge_info.magic_number, Cartridge_magic_number, CART_MAGIC_NUMBER_SIZE) != 0) {
		printf("Could not load cartridge \"%s\": Not a cartridge file.\n", path);
		goto load_end;
	}

	if(strncmp(Cartridge_info.cart_version, Cartridge_current_version, CART_VERSION_SIZE) != 0) {
		printf("Could not load cartridge \"%s\": Unsupported version.\n", path);
		goto load_end;
	}

	if(CART != NULL) {
		free(CART);
	}
	CART = malloc(CART_BANK_SIZE * CART_MAX_BANKS);

	uint8_t *mem = CART;
	for(uint8_t t=0; ok && t<CART_MAX_BANKS; ++t) {
		switch(Cartridge_info.bank_info[t]) {
			case CART_BANK_NONE: /* skip */ break;
			case CART_BANK_ROM: ok = !!x16read(cart, mem, CART_BANK_SIZE, 1); break;
			case CART_BANK_UNINITIALIZED_RAM: initialize_cart_bank(mem, randomize); break;
			case CART_BANK_INITIALIZED_RAM: ok = !!x16read(cart, mem, CART_BANK_SIZE, 1); break;
			case CART_BANK_UNINITIALIZED_NVRAM: initialize_cart_bank(mem, randomize); break;
			case CART_BANK_INITIALIZED_NVRAM:
				if(nvram) {
					ok = !!x16read(nvram, mem, CART_BANK_SIZE, 1);
					x16seek(cart, CART_BANK_SIZE, XSEEK_CUR);
				} else {
					ok = !!x16read(cart, mem, CART_BANK_SIZE, 1);
				}
				break;
			default: printf("Warning: Unknown cartridge bank type at %d\n", t); break;
		}
		mem += CART_BANK_SIZE;
	}

load_end:
	if(!ok) {
		free(CART);
		free(Cartridge_path);
		free(Cartridge_nvram_path);

		CART = NULL;
		Cartridge_path = NULL;
		Cartridge_nvram_path = NULL;
	}

	x16close(cart);
	x16close(nvram);

	return ok;
}

void cartridge_unload()
{
	if(CART != NULL) {
		free(CART);
		CART = NULL;
	}
	if(Cartridge_path != NULL) {
		free(Cartridge_path);
		Cartridge_path = NULL;
	}
	if(Cartridge_nvram_path != NULL) {
		free(Cartridge_nvram_path);
		Cartridge_nvram_path = NULL;
	}
}

void 
cartridge_new()
{
	cartridge_unload();

	memset(&Cartridge_info, 0, sizeof(struct x16cartridge_header));
	memcpy(Cartridge_info.magic_number, Cartridge_magic_number, CART_MAGIC_NUMBER_SIZE);
	memcpy(Cartridge_info.cart_version, Cartridge_current_version, CART_VERSION_SIZE);

	CART = malloc(CART_BANK_SIZE * CART_MAX_BANKS);
}

void 
cartridge_set_desc(const char *name)
{
	if(name == NULL) {
		return;
	}

	int namelen = strlen(name);
	if(namelen > CART_DESCRIPTION_SIZE) {
		namelen = CART_DESCRIPTION_SIZE;
	}

	memset(Cartridge_info.description, 0, CART_DESCRIPTION_SIZE);
	memcpy(Cartridge_info.description, name, namelen);
}

void
cartridge_set_author(const char *name)
{
	if(name == NULL) {
		return;
	}

	int namelen = strlen(name);
	if(namelen > CART_AUTHOR_SIZE) {
		namelen = CART_AUTHOR_SIZE;
	}

	memset(Cartridge_info.author, 0, CART_AUTHOR_SIZE);
	memcpy(Cartridge_info.author, name, namelen);
}

void
cartridge_set_copyright(const char *name)
{
	if(name == NULL) {
		return;
	}

	int namelen = strlen(name);
	if(namelen > CART_COPYRIGHT_SIZE) {
		namelen = CART_COPYRIGHT_SIZE;
	}

	memset(Cartridge_info.copyright, 0, CART_COPYRIGHT_SIZE);
	memcpy(Cartridge_info.copyright, name, namelen);

}

void
cartridge_set_program_version(const char *name)
{
	if(name == NULL) {
		return;
	}

	int namelen = strlen(name);
	if(namelen > CART_PROGRAM_VERSION_SIZE) {
		namelen = CART_PROGRAM_VERSION_SIZE;
	}

	memset(Cartridge_info.prg_version, 0, CART_PROGRAM_VERSION_SIZE);
	memcpy(Cartridge_info.prg_version, name, namelen);
}

bool 
cartridge_define_bank_range(uint8_t start_bank, uint8_t end_bank, uint8_t bank_type)
{
	if(start_bank < 32) {
		return false;
	}

	start_bank -= 32;

	if(start_bank >= CART_MAX_BANKS) {
		return false;
	}

	if(end_bank < 32) {
		return false;
	}

	end_bank -= 32;

	if(end_bank >= CART_MAX_BANKS) {
		return false;
	}

	if(start_bank > end_bank) {
		return false;
	}

	if(bank_type > CART_NUM_BANK_TYPES) {
		printf("Warning: Attempting to define unknown cartridge bank type %d\n", bank_type);
	}

	for(int i = start_bank; i <= end_bank; ++i) {
		Cartridge_info.bank_info[i] = bank_type;
	}

	return true;
}

bool
cartridge_import_files(char **bin_files, int num_files, uint8_t start_bank, uint8_t bank_type, uint32_t fill_value)
{
	if(start_bank < 32) {
		return false;
	}

	start_bank -= 32;

	if(start_bank >= CART_MAX_BANKS) {
		return false;
	}

	uint32_t start_address = ((uint32_t)start_bank) << 14;
	uint32_t available_size = CART_BANK_SIZE * CART_MAX_BANKS - start_address;

	for(int i=0; i<num_files; ++i) {
		if(available_size == 0) {
			return false;
		}

		struct x16file *bin = x16open(bin_files[i], "rb");
		if(bin == NULL) {
			return false;
		}

		const uint32_t data_read = (uint32_t)x16read(bin, CART + start_address, 1, available_size);
		available_size -= data_read;
		start_address += data_read;

		x16close(bin);
	}

	uint32_t fill_end = (start_address + CART_BANK_SIZE - 1) & (0xffUL << 14);
	if(fill_end > CART_BANK_SIZE * CART_MAX_BANKS) {
		fill_end = CART_BANK_SIZE * CART_MAX_BANKS;
	}
	memset(CART + start_address, fill_value, fill_end - start_address);
	
	uint8_t last_bank = (start_address - 1) >> 14;
	for(uint8_t i = start_bank; i <= last_bank; ++i) {
		Cartridge_info.bank_info[i] = bank_type;
	}
	return true;
}

bool
cartridge_fill(uint8_t start_bank, uint8_t end_bank, uint8_t bank_type, uint32_t fill_value)
{
	if(start_bank < 32) {
		return false;
	}

	start_bank -= 32;

	if(start_bank >= CART_MAX_BANKS) {
		return false;
	}

	if(end_bank < 32) {
		return false;
	}

	end_bank -= 32;

	if(end_bank >= CART_MAX_BANKS) {
		return false;
	}

	if(start_bank > end_bank) {
		return false;
	}

	uint32_t fill_start = ((uint32_t)start_bank) << 14;
	uint32_t fill_end = ((uint32_t)end_bank + 1) << 14;
	memset(CART + fill_start, fill_value, fill_end - fill_start);

	for(uint8_t i = start_bank; i <= end_bank; ++i) {
		Cartridge_info.bank_info[i] = bank_type;
	}
	return true;
}

bool 
cartridge_save(const char *path)
{
	const char *extension = find_extension(path, NULL);
	bool compressed = false;
	if(strcmp(extension, ".gz") == 0) {
		extension = find_extension(path, extension-1);
		compressed = true;
	}

	if(strncmp(extension, ".crt", 4) != 0) {
		printf("Path \"%s\" does not appear to be a cartridge (.crt) file.\n", path);
		return false;
	}

	struct x16file *cart = x16open(path, compressed ? "wb6" : "wb");

	uint64_t header_wrote = x16write(cart, (void*)(&Cartridge_info), sizeof(struct x16cartridge_header), 1);
	if(!header_wrote) {
		printf("Could not save cartridge \"%s\": Could not write header.\n", path);
		return false;
	}

	uint8_t *mem = CART;
	bool ok = true;
	for(uint8_t t=0; ok && t<CART_MAX_BANKS; ++t) {
		switch(Cartridge_info.bank_info[t]) {
			case CART_BANK_NONE: /* skip */ break;
			case CART_BANK_ROM: ok = !!x16write(cart, mem, CART_BANK_SIZE, 1); break;
			case CART_BANK_UNINITIALIZED_RAM: /* skip */ break;
			case CART_BANK_INITIALIZED_RAM: ok = !!x16write(cart, mem, CART_BANK_SIZE, 1); break;
			case CART_BANK_UNINITIALIZED_NVRAM: /* skip */ break;
			case CART_BANK_INITIALIZED_NVRAM: ok = !!x16write(cart, mem, CART_BANK_SIZE, 1); break;
			default: printf("Warning: Unknown cartridge bank type at %d\n", t); break;
		}
		mem += CART_BANK_SIZE;
	}

	if(!ok) {
		printf("Warning: Failed to save some cartridge data. The cartridge may be corrupt.\n");
	}

	x16close(cart);

	return ok;
}

bool
cartridge_save_nvram()
{
	if(Cartridge_nvram_path == NULL) {
		return false;
	}

	struct x16file *nvram = x16open(Cartridge_nvram_path, "rb");

	uint8_t *mem = CART;
	bool ok = true;
	for(uint8_t t=0; ok && t<CART_MAX_BANKS; ++t) {
		switch(Cartridge_info.bank_info[t]) {
			case CART_BANK_NONE: /* skip */ break;
			case CART_BANK_ROM: /* skip */ break;
			case CART_BANK_UNINITIALIZED_RAM: /* skip */ break;
			case CART_BANK_INITIALIZED_RAM: /* skip */ break;
			case CART_BANK_UNINITIALIZED_NVRAM: ok = !!x16write(nvram, mem, CART_BANK_SIZE, 1); break;
			case CART_BANK_INITIALIZED_NVRAM: ok = !!x16write(nvram, mem, CART_BANK_SIZE, 1); break;
			default: printf("Warning: Unknown cartridge bank type at %d\n", t); break;
		}
		mem += CART_BANK_SIZE;
	}

	if(!ok) {
		printf("Warning: Failed to save some nvram data. The nvram may be corrupt.\n");
	}

	x16close(nvram);

	return ok;
}

void 
cartridge_write(uint16_t address, uint8_t bank, uint8_t value)
{
	if(!CART) {
		return;
	}

	if(bank < 32) {
		return;
	}

	bank -= 32;

	switch(Cartridge_info.bank_info[bank]) {
		case CART_BANK_NONE: // fall-through
		case CART_BANK_ROM:
		default:
			break;

		case CART_BANK_UNINITIALIZED_RAM: // fall-through
		case CART_BANK_INITIALIZED_RAM: // fall-through
		case CART_BANK_UNINITIALIZED_NVRAM: // fall-through
		case CART_BANK_INITIALIZED_NVRAM:
			CART[((uint32_t)bank << 14) + address - 0xc000] = value;
			break;
	}
}

uint8_t 
cartridge_read(uint16_t address, uint8_t bank)
{
	if(!CART) {
		return 0;
	}

	if(bank < 32) {
		return 0;
	}

	bank -= 32;

	return CART[((uint32_t)bank << 14) + address - 0xc000];
}