#define _POSIX_C_SOURCE 200809L

#include "cartridge.h"
#include "files.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

extern uint8_t *CART;

uint8_t fill_value = 0;

static void parse_cmdline(int argc, char **argv);

static int x16_getline(char **line, size_t *n, FILE *f)
{
	if(line == NULL) {
		return -1;
	}
	if(n == NULL) {
		return -1;
	}
	if(f == NULL) {
		return -1;
	}
	if(*line == NULL) {
		*n = 64;
		*line = malloc(*n);
	} else if(*n == 0) {
		*n = 64;
		*line = realloc(*line, *n);
	}

	int i=EOF;
	int c;
	while((c = fgetc(f)) > EOF) {
		++i;
		if(i >= *n) {
			*n <<= 1;
			*line = realloc(*line, *n);
		}
		(*line)[i] = c;
		if(c == '\n') {
			break;
		}
	}
	if(c < 0 && i >= 0) {
		++i;
		if(i >= *n) {
			*n <<= 1;
			*line = realloc(*line, *n);
		}
		(*line)[i] = '\0';
	}
	if(i > 0) {
		if((*line)[i-1] == '\r') {
			(*line)[i-1] = '\0';
		} else {
			(*line)[i] = '\0';
		}
	} else if(i == 0) {
		(*line)[i] = '\0';
	}
	return i;
}

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
	printf("\tmemory. Value can be defined in decimal, in hexadecimal with a '$' or\n");
	printf("\t'0x' prefix, in octal with a leading 0, or in binary with a leading 0b or %%.\n");
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
	printf("\tleft to right.\n");

	exit(1);
}

static void
unpack_cart(const char *path, uint32_t bank_size)
{
	if(bank_size & 0x3fff) {
		printf("Warning: rom_size specified to -unpack was not 16KB-aligned, the resulting config file will not be trustworthy.\n");
	}

	const char *extension = file_find_extension(path, NULL);
	size_t extlen = strlen(extension);

	size_t root_pathlen = strlen(path) - extlen;
	char *root_path = malloc(root_pathlen + 1);
	memcpy(root_path, path, root_pathlen);
	root_path[root_pathlen] = '\0';

	size_t outpath_len = root_pathlen + 8; // strlen("_000.bin");
	char *outpath = malloc(outpath_len + 1);

	size_t cfgpath_len = root_pathlen + 4; // strlen(".cfg");
	char *cfgpath = malloc(cfgpath_len + 1);
	sprintf(cfgpath, "%s.cfg", root_path);

	FILE *cfg = fopen(cfgpath, "wb");
	if(cfg == NULL) {
		printf("Error: Could not open config file for writing: %s\n", cfgpath);
	} 

	cartridge_new();
	if(cartridge_load(path, true)) {
		if(cfg != NULL) {
			char buffer[65];

			cartridge_get_desc(buffer, 65);
			fprintf(cfg, "-desc \"%s\"\n", buffer);

			cartridge_get_author(buffer, 65);
			fprintf(cfg, "-author \"%s\"\n", buffer);

			cartridge_get_copyright(buffer, 65);
			fprintf(cfg, "-copyright \"%s\"\n", buffer);

			cartridge_get_program_version(buffer, 65);
			fprintf(cfg, "-prg_version \"%s\"\n", buffer);
		}
		
		uint32_t bank_idx = 0;
		for(uint32_t offset = 0; offset < CART_MAX_SIZE; offset += bank_size) {
			sprintf(outpath, "%s_%03d.bin", root_path, bank_idx);
			++bank_idx;

			struct x16file *f = x16open(outpath, "wb");
			if(f == NULL) {
				printf("Error opening file for write: %s\n", outpath);
				continue;
			}
			size_t avail = CART_MAX_SIZE - offset;
			if(!x16write(f, CART + offset, bank_size > avail ? avail : bank_size, 1)) {
				printf("Error writing to file: %s\n", outpath);
			}
			x16close(f);

			uint32_t start_bank = offset >> 14;
			uint32_t end_bank = ((offset + bank_size) >> 14) - 1;

			uint8_t start_bank_type = cartridge_get_bank_type(start_bank + 32);
			bool conflicted = false;
			for(uint32_t bank = start_bank + 1; bank <= end_bank; ++bank) {
				uint8_t bank_type = cartridge_get_bank_type(bank + 32);
				if(bank_type != start_bank_type) {
					if(!conflicted) {
						printf("Warning: Discovered bank type mismatch while writing file: %s\n", outpath);
						printf("\tFile starts at offset $%08x, which is banks %d through %d\n", offset, start_bank, end_bank);
						conflicted = true;
					}
					if(start_bank == bank - 1) {
						printf("\tBank %d is type %d\n", start_bank + 32, start_bank_type);
					} else {
						printf("\tBanks %d through %d are type %d\n", start_bank + 32, bank + 32 - 1, start_bank_type);
					}
					start_bank_type = bank_type;
					start_bank = bank;
				}
			}
			if(conflicted) {
				if(start_bank == end_bank) {
					printf("\tBank %d is type %d\n", start_bank + 32, start_bank_type);
				} else {
					printf("\tBanks %d through %d are type %d\n", start_bank + 32, end_bank + 32, start_bank_type);
				}
				printf("\tThe resulting config file will not be trustworthy.");
			}

			if(cfg != NULL) {
				start_bank = offset >> 14;
				switch(cartridge_get_bank_type(offset >> 14)) {
					case CART_BANK_NONE: /* skip */ break;
					case CART_BANK_ROM: fprintf(cfg, "-rom_file %d \"%s\"\n", start_bank, outpath); break;
					case CART_BANK_UNINITIALIZED_RAM: fprintf(cfg, "-ram %d %d\n", start_bank, end_bank); break;
					case CART_BANK_INITIALIZED_RAM: fprintf(cfg, "-ram_file %d \"%s\"\n", start_bank, outpath); break;
					case CART_BANK_UNINITIALIZED_NVRAM: fprintf(cfg, "-nvram %d %d\n", start_bank, end_bank); break;
					case CART_BANK_INITIALIZED_NVRAM: fprintf(cfg, "-nvram_file %d \"%s\"\n", start_bank, outpath); break;
					default: /* error */ break;
				}
			}
		}
	}

	fclose(cfg);

	free(cfgpath);
	free(outpath);
	free(root_path);
}

static char *
next_token(char **token)
{
	if(**token == '\0' || **token == '\r' || **token == '\n') {
		return NULL;
	}
	char *start = *token;
	while(!isgraph(*start) && *start) {
		++start;
	}

	char *next = start;
	if(*next == '"') {
		++start;
		++next;
		while(*next != '"' && *next) {
			if(*next == '\\') {
				++next;
			}
			++next;
		}
	} else {
		while(isgraph(*next) && *next) {
			if(*next == '\\') {
				++next;
			}
			++next;
		}
	}
	if(*next) {
		*next = '\0';
		*token = next+1;
	} else {
		*token = next;
	}
	
	return start;
}

static void
parse_config(const char *path)
{
	FILE *cfg = fopen(path, "rb");
	if(cfg == NULL) {
		printf("Error opening file for read: %s", path);
		return;
	}

	size_t argslen = 32;
	char **argv = malloc(argslen * sizeof(char **));

	char *line = NULL;
	size_t n = 0;
	while(x16_getline(&line, &n, cfg) >= 0) {
		char *token = line;
		int argc = 0;
		char *cmd;

		while((cmd = next_token(&token))) {
			if(strlen(cmd) == 0) {
				continue;
			}
			if(argc >= argslen) {
				argslen <<= 1;
				argv = realloc(argv, argslen * sizeof(char **));
			}
			argv[argc] = cmd;
			++argc;
		}

		if(argc > 0) {
			parse_cmdline(argc, argv);
		}
	}

	free(line);
	free(argv);
}

static void 
parse_cmdline(int argc, char **argv)
{
	while(argc > 0) {
		if (!strcmp(argv[0], "-help")) {
			usage();

		} else if (!strcmp(argv[0], "-cfg")) {
			++argv;
			--argc;

			if (!argc || argv[0][0] == '-') {
				usage();
			}

			parse_config(argv[0]);
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

			if(argv[0][0] == '$') {
				fill_value = strtol(argv[0] + 1, NULL, 16);
			} else if(argv[0][0] == '%') {
				fill_value = strtol(argv[0] + 1, NULL, 2);
			} else if(argv[0][0] == '0') {
				if(argv[0][1] == 'x') {
					fill_value = strtol(argv[0] + 2, NULL, 16);
				} else if(argv[0][1] == 'b') {
					fill_value = strtol(argv[0] + 2, NULL, 2);
				} else {
					fill_value = strtol(argv[0] + 1, NULL, 8);
				}
			} else {
				fill_value = strtol(argv[0], NULL, 10);
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
				cartridge_fill(start_bank, start_bank, CART_BANK_INITIALIZED_NVRAM, fill_value);
				continue;
			}

			uint8_t end_bank = atoi(argv[0]);
			++argv;
			--argc;

			cartridge_fill(start_bank, end_bank, CART_BANK_INITIALIZED_NVRAM, fill_value);

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

			cartridge_save(argv[0]);
			++argv;
			--argc;

		} else if(!strcmp(argv[0], "-unpack")) {
			++argv;
			--argc;

			if (!argc || argv[0][0] == '-') {
				usage();
			}

			char *unpack_file = argv[0];
			++argv;
			--argc;

			if (!argc || argv[0][0] == '-') {
				unpack_cart(unpack_file, CART_BANK_SIZE);
				continue;
			}

			uint32_t unpack_size = atoi(argv[0]);
			++argv;
			--argc;

			unpack_cart(unpack_file, unpack_size);

		} else if(!strcmp(argv[0], "-testbins")) {
			++argv;
			--argc;

			char test_path[256];
			for(uint8_t b=0; b<CART_MAX_BANKS; ++b) {
				sprintf(test_path, "test-%03d.bin", b);
				struct x16file *f = x16open(test_path, "wb");
				if(f == NULL) {
					printf("Error opening file for write: %s\n", test_path);
					continue;
				}
				for(int i=0; i<CART_BANK_SIZE; ++i) {
					x16write(f, &b, 1, 1);
				}
				x16close(f);
			}
		} else {
			return usage();
		}
	}
}

int 
main(int argc, char **argv)
{
	argc--;
	argv++;

	cartridge_new();
	parse_cmdline(argc, argv);

	return 0;
}
