#include "../src/cartridge.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

extern uint8_t *CART;

void
usage()
{
	printf("-help\n");
	printf("\tPrint this message and exit.\n");
	printf("\n");
	printf("-cfg <filename.cfg>\n");
	printf("\tUse this file to pack the cartridge data. Config file is simply the\n");
	printf("\tcommand line switches, one per line.\n");
	printf("\n");
	printf("-desc \"Name/Description\"\n");
	printf("\tSet the description field of the cartridge file. This is limited to 32\n");
	printf("\tbytes and padded with null characters ('\\0').\n");
	printf("\n");
	printf("-author \"Author Information\"\n");
	printf("\tSet the author field of the cartridge file. This is limited to 32 bytes\n");
	printf("\tand padded with null characters ('\\0').\n");
	printf("\n");
	printf("-copyright \"Copyright Information\"\n");
	printf("\tSet the copyright field of the cartridge file. This is limited to 32\n");
	printf("\tbytes and padded with null characters ('\\0').\n");
	printf("\n");
	printf("-version \"Version Information\"\n");
	printf("\tSet the version field of the cartridge file. This is limited to 32 bytes\n");
	printf("\tand padded with null characters ('\\0').\n");
	printf("\n");
	printf("-fill <value>\n");
	printf("\tSet the fill value to use with any partially-filled banks of cartridge\n");
	printf("\tmemory. Value can be defined in decimal, or in hexadecimal with a '$' or\n");
	printf("\t'0x' prefix. 8-bit values will be repeated every byte, 16-bit values every two\n");
	printf("\tbytes, and 32-bit values every 4 bytes.\n");
	printf("\n");
	printf("-rom_file <start_bank> [<filenames.bin>...]\n");
	printf("\tDefine rom banks from the specified list of files. File data is tightly\n");
	printf("\tpacked -- if a file does not end on a 16KB interval, the next file will be inserted\n");
	printf("\timmediately after it within the same bank. If the last file does not end on a 16KB\n");
	printf("\tinterval, the remainder of the rom will be filled with the value set by '-fill'.\n");
	printf("\n");
	printf("-ram <start_bank> [<end bank>]\n");
	printf("\tDefine one or more banks of uninitialized RAM.\n");
	printf("\n");
	printf("-ram_file <start_bank> [<filenames.bin>...]\n");
	printf("\tDefine pre-initialized ram banks from the specified list of files. File\n");
	printf("\tdata is tightly packed like with -rom. If the last file does not end on a 16KB\n");
	printf("\tinterval, the remainder of the rom will be filled with the value set by '-fill'.\n");
	printf("\n");
	printf("-nvram <start_bank> [<end_bank>]\n");
	printf("\tDefine one or more uninitialized nvram banks.\n");
	printf("\n");
	printf("-nvram_file <start_bank> [<filenames.bin>...]\n");
	printf("\tDefine pre-initialized nvram banks from the specified list of files. File\n");
	printf("\tdata is tightly packed like with -rom. If the last file does not end on a 16KB\n");
	printf("\tinterval, the remainder of the rom will be filled with the value set by '-fill'.\n");
	printf("\n");
	printf("-nvram_value <start_bank> <end_bank>\n");
	printf("\tDefine pre-initialized nvram banks with the value set by '-fill'.\n");
	printf("\n");
	printf("-none <start_bank> [<end_bank>]\n");
	printf("\tDefine one or more unpopulated banks of the cartridge. By default, all\n");
	printf("\tbanks are unpopulated unless specified by a previous command-line option.\n");
	printf("\n");
	printf("-o <output.crt>\n");
	printf("\tSet the filename of the output cartridge file.\n");
	printf("\n");
	printf("-unpack <input.crt> [<rom_size>]\n");
	printf("\tUnpacks the binary data from the cartridge file into <rom_size> slices.\n");
	printf("\t(for use with an EPROM programmer.) The ouput files will be the same\n");
	printf("\tfilename as the input file, with _### appended. This will also create a\n");
	printf("\t.cfg file that can be used to re-pack the files into a new CRT if needed.\n");
	printf("\n");
	printf("All options can be specified multiple times, and are applied  in-order from\n");
	printf("\tleft to right. For -desc, -o, and -unpack, it is legal to specify them\n");
	printf("\tmultiple times but only the right-most instances of each will have effect.\n");

	exit(1);
}

static void
unpack_cart(const char *path, uint32_t bank_size)
{
	cartridge_new();
	if(cartridge_load(path, false)) {
	}
}

static void
parse_config(const char *path)
{
	
}

int 
main(int argc, char **argv)
{
	argc--;
	argv++;

	char *config_file = NULL;
	char *output_file = NULL;
	char *unpack_file = NULL;
	uint32_t unpack_size = CART_BANK_SIZE;
	uint32_t fill_value = 0;

	cartridge_new();

	while(argc > 0) {
		if (!strcmp(argv[0], "-help")) {
			usage();

		} else if (!strcmp(argv[0], "-cfg")) {
			++argv;
			--argc;

			if (!argc || argv[0][0] == '-') {
				usage();
			}

			config_file = argv[0];
			++argv;
			--argc;

		} else if (!strcmp(argv[0], "-desc")) {
			++argv;
			--argc;

			if (!argc || argv[0][0] == '-') {
				usage();
			}

			cartridge_set_desc(argv[0]);
			++argv;
			--argc;

		} else if (!strcmp(argv[0], "-author")) {
			++argv;
			--argc;

			if (!argc || argv[0][0] == '-') {
				usage();
			}

			cartridge_set_author(argv[0]);
			++argv;
			--argc;

		} else if (!strcmp(argv[0], "-copyright")) {
			++argv;
			--argc;

			if (!argc || argv[0][0] == '-') {
				usage();
			}

			cartridge_set_copyright(argv[0]);
			++argv;
			--argc;

		} else if (!strcmp(argv[0], "-version")) {
			++argv;
			--argc;

			if (!argc || argv[0][0] == '-') {
				usage();
			}

			cartridge_set_program_version(argv[0]);
			++argv;
			--argc;

		} else if(!strcmp(argv[0], "-fill")) {
			++argv;
			--argc;

			if (!argc || argv[0][0] == '-') {
				usage();
			}

			fill_value = atoi(argv[0]);
			++argv;
			--argc;

			if((fill_value & 0xffffff00) == 0) {
				fill_value |= (fill_value << 8);
			}
			if((fill_value & 0xffff0000) == 0) {
				fill_value |= (fill_value << 16);
			}

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

			cartridge_import_files(files, num_files, start_bank, CART_BANK_ROM, fill_value);

		} else if(!strcmp(argv[0], "-ram")) {
			++argv;
			--argc;

			if (!argc || argv[0][0] == '-') {
				usage();
			}

			uint8_t start_bank = atoi(argv[0]);
			++argv;
			--argc;

			if (!argc || argv[0][0] == '-') {
				cartridge_define_bank_range(start_bank, start_bank, CART_BANK_UNINITIALIZED_RAM);
				continue;
			}

			uint8_t end_bank = atoi(argv[0]);
			++argv;
			--argc;

			cartridge_define_bank_range(start_bank, end_bank, CART_BANK_UNINITIALIZED_RAM);

		} else if(!strcmp(argv[0], "-ram_file")) {
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

			cartridge_import_files(files, num_files, start_bank, CART_BANK_INITIALIZED_RAM, fill_value);

		} else if(!strcmp(argv[0], "-nvram")) {
			++argv;
			--argc;

			if (!argc || argv[0][0] == '-') {
				usage();
			}

			uint8_t start_bank = atoi(argv[0]);
			++argv;
			--argc;

			if (!argc || argv[0][0] == '-') {
				cartridge_define_bank_range(start_bank, start_bank, CART_BANK_UNINITIALIZED_NVRAM);
				continue;
			}

			uint8_t end_bank = atoi(argv[0]);
			++argv;
			--argc;

			cartridge_define_bank_range(start_bank, end_bank, CART_BANK_UNINITIALIZED_NVRAM);

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

			cartridge_import_files(files, num_files, start_bank, CART_BANK_INITIALIZED_NVRAM, fill_value);

		} else if(!strcmp(argv[0], "-nvram_value")) {
			++argv;
			--argc;

			if (!argc || argv[0][0] == '-') {
				usage();
			}

			uint8_t start_bank = atoi(argv[0]);
			++argv;
			--argc;

			if (!argc || argv[0][0] == '-') {
				cartridge_fill(start_bank, start_bank, CART_BANK_UNINITIALIZED_NVRAM, fill_value);
				continue;
			}

			uint8_t end_bank = atoi(argv[0]);
			++argv;
			--argc;

			cartridge_fill(start_bank, end_bank, CART_BANK_UNINITIALIZED_NVRAM, fill_value);

		} else if(!strcmp(argv[0], "-none")) {
			++argv;
			--argc;

			if (!argc || argv[0][0] == '-') {
				usage();
			}

			uint8_t start_bank = atoi(argv[0]);
			++argv;
			--argc;

			if (!argc || argv[0][0] == '-') {
				cartridge_define_bank_range(start_bank, start_bank, CART_BANK_NONE);
				continue;
			}

			uint8_t end_bank = atoi(argv[0]);
			++argv;
			--argc;

			cartridge_define_bank_range(start_bank, end_bank, CART_BANK_NONE);

		} else if(!strcmp(argv[0], "-o")) {
			++argv;
			--argc;

			if (!argc || argv[0][0] == '-') {
				usage();
			}

			output_file = argv[0];
			++argv;
			--argc;

		} else if(!strcmp(argv[0], "-unpack")) {
			++argv;
			--argc;

			if (!argc || argv[0][0] == '-') {
				usage();
			}

			unpack_file = argv[0];
			++argv;
			--argc;

			if (!argc || argv[0][0] == '-') {
				continue;
			}

			unpack_size = atoi(argv[0]);
			++argv;
			--argc;
		}
	}

	if(config_file) {
		parse_config(config_file);
	}

	if(output_file) {
		cartridge_save(output_file);
	}

	if(unpack_file) {
		unpack_cart(unpack_file, unpack_size);
	}

	return 0;
}