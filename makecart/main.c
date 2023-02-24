#include "../src/cartridge.h"

#include <stdlib.h>
#include <stdio.h>

void
usage()
{
	printf("-desc \"Name/Description\"\n");
	printf("\tSet the description field of the cartridge file. This is limited to 28 bytes and padded with null characters ('\\0').\n");
	printf("\n");
	printf("-fill <value>\n");
	printf("\tSet the fill value to use with any partially-filled banks of cartridge memory. Value can be defined in decimal, or in hexadecimal with a '$' or '0x' prefix. 8-bit values will be repeated every byte, 16-bit values every two bytes, and 32-bit values every 4 bytes.\n");
	printf("\n");
	printf("-rom_file <start_bank> [<filenames.bin>...]\n");
	printf("\tDefine rom banks from the specified list of files. File data is tightly packed -- if a file does not end on a 16KB interval, the next file will be inserted immediately after it within the same bank. If the last file does not end on a 16KB interval, the remainder of the rom will be filled with the value set by '-fill'.\n");
	printf("\n");
	printf("-nvram_file <start_bank> [<filenames.bin>...]\n");
	printf("\tDefine pre-initialized nvram banks from the specified list of files. File data is tightly packed like with -rom. If the last file does not end on a 16KB interval, the remainder of the rom will be filled with the value set by '-fill'.\n");
	printf("\n");
	printf("-nvram_value <start_bank> <end_bank>\n");
	printf("\tDefine pre-initialized nvram banks with the value set by '-fill'.\n");
	printf("\n");
	printf("-nvram <start_bank> [<end_bank>]\n");
	printf("\tDefine one or more uninitialized nvram banks.\n");
	printf("\n");
	printf("-ram <start_bank> [<end bank>]\n");
	printf("\tDefine one or more banks of RAM.\n");
	printf("\n");
	printf("-none <start_bank> [<end_bank>]\n");
	printf("\tDefine one or more unpopulated banks of the cartridge. By default, all banks are unpopulated unless specified by a previous command-line option.\n");
	printf("\n");
	printf("-o <output.crt>\n");
	printf("\tSet the filename of the output cartridge file.\n");
	printf("\n");
	printf("All options can be specified multiple times, and are applied  in-order from left to right. For -desc and -o, it is legal to specify them multiple times but only the right-most instances of each will have effect.\n");
	exit(1);
}

int main(int argc, char **argv)
{
	argc--;
	argv++;

	char *output_file = NULL;
	uint32_t fill_value = 0;

	cartridge_new();

	while(argc > 0) {
		if (!strcmp(argv[0], "-desc")) {
			++argv;
			--argc;

			if (!argc || argv[0][0] == '-') {
				usage();
			}

			cartridge_set_desc(argv[0]);
			++argv;
			--argc;

		} else if(!strcmp(argv[0], "-fill")) {
			++argv;
			--argc;

			if (!argc || argv[0][0] == '-') {
				usage();
			}

			fill_value = atoi(argv[0]);
			if((fill_value & 0xffffff00) == 0) {
				fill_value |= (fill_value << 8);
			}
			if((fill_value & 0xffff0000) == 0) {
				fill_value |= (fill_value << 16);
			}
			++argv;
			--argc;

		} else if(!strcmp(argv[0], "-rom_file")) {
			++argv;
			--argc;

			if (!argc || argv[0][0] == '-') {
				usage();
			}

			uint8_t start_bank = atoi(argv[0]);
			++argv;
			--argc;

			if (!argc || argv[0][0] == '-') {
				usage();
			}

			char **files = &argv[0];
			int num_files = 1;
			while (argc && argv[0][0] != '-') {
				++argv;
				--argc;
			}

			cartridge_import_files(files, num_files, start_bank, CART_BANK_ROM);

		} else if(!strcmp(argv[0], "-nvram_file")) {
			++argv;
			--argc;

			if (!argc || argv[0][0] == '-') {
				usage();
			}

			uint8_t start_bank = atoi(argv[0]);
			++argv;
			--argc;

			if (!argc || argv[0][0] == '-') {
				usage();
			}

			char **files = &argv[0];
			int num_files = 1;
			while (argc && argv[0][0] != '-') {
				++argv;
				--argc;
			}

			cartridge_import_files(files, num_files, start_bank, CART_BANK_INITIALIZED_NVRAM);

		} else if(!strcmp(argv[0], "-nvram_value")) {
		} else if(!strcmp(argv[0], "-ram")) {
		} else if(!strcmp(argv[0], "-none")) {
		} else if(!strcmp(argv[0], "-o")) {
		}
	}

	return 0;
}