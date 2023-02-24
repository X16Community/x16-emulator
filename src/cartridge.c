#include "cartridge.h"
#include "files.h"

#include <string.h>

struct x16cartridge_header Cartridge_info;
uint8_t *CART = NULL;
const char *Cartridge_path = NULL;
const char *Cartridge_nvram_path = NULL;

const uint8_t Cartridge_magic_number[2] = { 0x78, 0x43 };
const uint8_t Cartridge_current_major_version = 1;
const uint8_t Cartridge_current_minor_version = 0;

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
	Cartridge_path = path;

	struct x16file *cart = x16open("cartridge.crt", "rb");
	struct x16file *nvram = x16open("cartridge.nvram", "rb");

	x16read(cart, &Cartridge_info, sizeof(struct x16cartridge_header), 1);

	if(memcmp(Cartridge_info.magic_number, Cartridge_magic_number, 2) != 0) {
		return false;
	}

	CART = malloc(CART_BANK_SIZE * CART_MAX_BANKS);

	uint8_t *mem = CART;
	for(uint8_t t=0; t<CART_MAX_BANKS; ++t) {
		switch(Cartridge_info.bank_info[t]) {
			case CART_BANK_NONE: /* skip */ break;
			case CART_BANK_ROM: x16read(cart, mem, CART_BANK_SIZE, 1); break;
			case CART_BANK_RAM: initialize_cart_bank(mem, randomize); break;
			case CART_BANK_INITIALIZED_NVRAM:
				if(nvram) {
					x16read(nvram, mem, CART_BANK_SIZE, 1);
					x16seek(cart, CART_BANK_SIZE, XSEEK_CUR);
				} else {
					x16read(cart, mem, CART_BANK_SIZE, 1);
				}
				break;
			case CART_BANK_UNINITIALIZED_NVRAM: initialize_cart_bank(mem, randomize); break;
			default: printf("Warning: Unknown cartridge bank type at %d\n", t); break;
		}
		mem += CART_BANK_SIZE;
	}
}

void 
cartridge_new()
{
	memset(&Cartridge_info, 0, sizeof(struct x16cartridge_header));
	memcpy(Cartridge_info.magic_number, Cartridge_magic_number, 2);
	Cartridge_info.major_version = Cartridge_current_major_version;
	Cartridge_info.minor_version = Cartridge_current_minor_version;
}

void 
cartridge_set_desc(const char *name)
{
	int namelen = strlen(name);
	if(namelen > CART_DESCRIPTION_SIZE) {
		namelen = CART_DESCRIPTION_SIZE;
	}

	memset(Cartridge_info.description, 0, CART_DESCRIPTION_SIZE);
	memset(Cartridge_info.description, name, namelen);
}

void 
cartridge_define_banks(uint8_t start_bank, uint8_t num_banks, uint8_t bank_type)
{
	uint8_t end_bank = start_bank + num_banks - 1;
	if(end_bank >= CART_MAX_BANKS) {
		printf("Warning: Attempting to define cartridge banks past end-of-cartridge, truncating.\n");
		end_bank = CART_MAX_BANKS-1;
	}

	cartridge_define_bank_range(start_bank, end_bank, bank_type);
}

void 
cartridge_define_bank_range(uint8_t start_bank, uint8_t end_bank, uint8_t bank_type)
{
	if(bank_type > CART_BANK_UNINITIALIZED_NVRAM) {
		printf("Warning: Attempting to define unknown cartridge bank type %d\n", bank_type);
	}

	for(int i=start_bank; i<end_bank; ++i) {
		Cartridge_info.bank_info[i] = bank_type;
	}
}

bool
cartridge_import_files(const char **bin_files, int num_files, uint8_t start_bank, uint8_t bank_type, uint32_t fill_value)
{
	uint32_t start_address = ((uint32_t)start_bank) << 14;
	uint32_t available_size = CART_BANK_SIZE * CART_MAX_BANKS - start_address;

	for(int i=0; i<num_files; ++i) {
		struct x16file *bin = x16open(bin_files[i], "rb");
		if(bin == NULL) {
			return false;
		}

		const uint32_t data_read = (uint32_t)x16read(bin, CART + start_address, 1, available_size);
		available_size -= data_read;
		start_address += data_read;
	}

	uint32_t fill_end = (start_address + CART_BANK_SIZE - 1) & (224UL << 14);
	if(fill_end > CART_BANK_SIZE * CART_MAX_BANKS) {
		fill_end = CART_BANK_SIZE * CART_MAX_BANKS;
	}
	memset(CART + start_address, fill_value, fill_end - start_address);
	
	uint8_t last_bank = (start_address - 1) >> 14;
	for(uint8_t i = start_bank; i <= last_bank; ++i) {
		Cartridge_info.bank_info[i] = bank_type;
	}
}

bool
cartridge_fill(uint8_t start_bank, uint8_t num_banks, uint8_t bank_type, uint32_t fill_value)
{

}

bool 
cartridge_save(const char *cartridge_file)
{

}

bool
cartridge_save_nvram()
{

}

void 
cartridge_write(uint16_t address, uint8_t bank, uint8_t value)
{
	if(!CART) {
		return;
	}

	switch(Cartridge_info.bank_info[bank]) {
		case CART_BANK_NONE: // fall-through
		case CART_BANK_ROM:
		default:
			break;

		case CART_BANK_RAM: // fall-through
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

	return CART[((uint32_t)bank << 14) + address - 0xc000];
}