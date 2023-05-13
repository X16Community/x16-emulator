// Commander X16 Emulator
// Copyright (c) 2019 Michael Steil
// Copyright (c) 2020 Frank van den Hoef
// All rights reserved. License: 2-clause BSD
#ifndef __APPLE__
#define _XOPEN_SOURCE   600
#define _POSIX_C_SOURCE 1
#endif

#include <limits.h>
#include <stdio.h>
#include <stdbool.h>
#include "sdcard.h"
#include "files.h"

//#define VERBOSE 1

// MMC/SD command (SPI mode)
enum {
	CMD0   = 0,         // GO_IDLE_STATE
	CMD1   = 1,         // SEND_OP_COND
	ACMD41 = 0x80 | 41, // SEND_OP_COND (SDC)
	CMD8   = 8,         // SEND_IF_COND
	CMD9   = 9,         // SEND_CSD
	CMD10  = 10,        // SEND_CID
	CMD12  = 12,        // STOP_TRANSMISSION
	CMD13  = 13,        // SEND_STATUS
	ACMD13 = 0x80 | 13, // SD_STATUS (SDC)
	CMD16  = 16,        // SET_BLOCKLEN
	CMD17  = 17,        // READ_SINGLE_BLOCK
	CMD18  = 18,        // READ_MULTIPLE_BLOCK
	CMD23  = 23,        // SET_BLOCK_COUNT
	ACMD23 = 0x80 | 23, // SET_WR_BLK_ERASE_COUNT (SDC)
	CMD24  = 24,        // WRITE_BLOCK
	CMD25  = 25,        // WRITE_MULTIPLE_BLOCK
	CMD32  = 32,        // ERASE_WR_BLK_START
	CMD33  = 33,        // ERASE_WR_BLK_END
	CMD38  = 38,        // ERASE
	CMD55  = 55,        // APP_CMD
	CMD58  = 58,        // READ_OCR
};

static char sdcard_path[PATH_MAX] = "";
static struct x16file *sdcard_file = NULL;
bool sdcard_attached = false;

static uint8_t rxbuf[3 + 512];
static int rxbuf_idx;
static uint32_t lba;
static uint8_t last_cmd;
static bool is_acmd = false;
static bool is_idle = true;
static bool is_initialized = false;

static const uint8_t *response = NULL;
static int response_length = 0;
static int response_counter = 0;

static bool selected = false;

void
sdcard_set_path(char const *path)
{
	sdcard_detach();

	strncpy(sdcard_path, path, PATH_MAX);
	sdcard_path[PATH_MAX-1] = '\0';

	sdcard_attach();
}

bool
sdcard_path_is_set()
{
	return strlen(sdcard_path) > 0;
}

void
sdcard_attach()
{
	if (!sdcard_attached && sdcard_path_is_set()) {
		sdcard_file = x16open(sdcard_path, "r+b");
		if(sdcard_file == NULL) {
			printf("Cannot open SDCard file %s!\n", sdcard_path);
			return;
		}

		printf("SD card attached.\n");
		sdcard_attached = true;
		is_initialized = false;
	}
}

void
sdcard_detach()
{
	if (sdcard_attached) {
		x16close(sdcard_file);
		sdcard_file = NULL;

		printf("SD card detached.\n");
		sdcard_attached = false;
	}
}

void
sdcard_select(bool select)
{
	selected = select;
	rxbuf_idx = 0;
#if defined(VERBOSE) && VERBOSE >= 2
	printf("*** SD card select: %u\n", select);
#endif
}

static void
set_response_csd(void)
{
	static uint8_t rr[] = {
		0xff, // dummy
		0xff, // dummy
		0x00, // R1 response
		0xff, // dummy
		0xfe, // begin block
		0x40, // CSD_STRUCTURE [7:6] = 1, RESERVED [5:0] = 0
		0x0e, // TAAC = 0x0e
		0x00, // NSAC = 0x00
		0x32, // TRAN_SPEED = 0x32
		0x5b, // CCC (11:4)
		0x59, // CCC (3:0) [7-4], READ_BL_LEN [3-0]
		0x00, // READ_BL_PARTIAL [7], WRITE_BLK_MISALIGN [6], READ_BLK_MISALIGN [5], DSR_IMP [4], RESERVED [3:0]
		0x00, // RESERVED [7:6], C_SIZE(21:16) [5:0]
		0x00, // C_SIZE(15:8)
		0x00, // C_SIZE(7:0)
		0x7f, // RESERVED [7], ERASE_BLK_EN[6], SECTOR_SIZE(6:1) [5:0]
		0x80, // SECTOR_SIZE(0) [7], WP_GRP_SIZE [6:0]
		0x0a, // WP_GRP_ENABLE [7], RESERVED [6:5], R2W_FACTOR [4:2], WRITE_BL_LEN (3:2) [1:0]
		0x40, // WRITE_BL_LEN (1:0) [7:6], WRITE_BL_PARTIAL [5], RESERVED [4:0]
		0x00, // FILE_FORMAT_GRP [7] = 0, COPY [6], PERM_WRITE_PROTECT [5], TMP_WRITE_PROTECT [4], RESERVED [3:0]
		0x01 // CRC[7:1], ALWAYS_1 [0]
		};
	uint64_t c_size = (x16size(sdcard_file) >> 19)-1;
	rr[12] |= (c_size >> 16) & 0x3f;
	rr[13] = (c_size >> 8) & 0xff;
	rr[14] = c_size & 0xff;

	response = rr;
	response_length = 21;
}

static void
set_response_r1(void)
{
	static uint8_t r1;
	r1 = is_idle ? 1 : 0;
	response = &r1;
	response_length = 1;
}

static void
set_response_r2(void)
{
	if (is_initialized) {
		static const uint8_t r2[] = {0x00, 0x00};
		response = r2;
		response_length = sizeof(r2);
	} else {
		static const uint8_t r2[] = {0x1F, 0xFF};
		response = r2;
		response_length = sizeof(r2);
	}
}

static void
set_response_r3(void)
{
	static const uint8_t r3[] = {0xC0, 0xFF, 0x80, 0x00};
	response = r3;
	response_length = sizeof(r3);
}

static void
set_response_r7(void)
{
	static const uint8_t r7[] = {1, 0x00, 0x00, 0x01, 0xAA};
	response = r7;
	response_length = sizeof(r7);
}

uint8_t
sdcard_handle(uint8_t inbyte)
{
	if (!selected || (sdcard_file == NULL)) {
		return 0xFF;
	}
	// printf("sdcard_handle: %02X\n", inbyte);

	uint8_t outbyte = 0xFF;

	if (rxbuf_idx == 0 && inbyte == 0xFF) {
		// send response data
		if (response) {
			outbyte = response[response_counter++];
			if (response_counter == response_length) {
				response = NULL;
			}
		}

	} else {
		rxbuf[rxbuf_idx++] = inbyte;

		if ((rxbuf[0] & 0xC0) == 0x40 && rxbuf_idx == 6) {
			rxbuf_idx = 0;

			// Check for start-bit + transmission bit
			if ((rxbuf[0] & 0xC0) != 0x40) {
				response = NULL;
				return 0xFF;
			}
			rxbuf[0] &= 0x3F;

			// Use upper command bit to indicate this is an ACMD
			if (is_acmd) {
				rxbuf[0] |= 0x80;
				is_acmd = false;
			}

			last_cmd = rxbuf[0];

#if defined(VERBOSE) && VERBOSE >= 2
			printf("*** SD %sCMD%d -> Response:", (rxbuf[0] & 0x80) ? "A" : "", rxbuf[0] & 0x3F);
#endif
			switch (rxbuf[0]) {
				case CMD0: {
					// GO_IDLE_STATE: Resets the SD Memory Card
					is_idle = true;
					set_response_r1();
					break;
				}

				case CMD8: {
					// SEND_IF_COND: Sends SD Memory Card interface condition that includes host supply voltage
					set_response_r7();
					break;
				}

				case CMD9: {
					// SEND_CSD: Sends card-specific data
					set_response_csd();
					break;
				}

				case ACMD41: {
					// SD_SEND_OP_COND: Sends host capacity support information and activated the card's initialization process
					is_idle = false;
					is_initialized = true;
					set_response_r1();
					break;
				}

				case CMD13: {
					// SEND_STATUS: Asks the selected card to send its status register
					set_response_r2();
					break;
				}
				case CMD16: {
					// SET_BLOCKLEN: In case of non-SDHC card, this sets the block length. Block length of SDHC/SDXC cards are fixed to 512 bytes.
					set_response_r1();
					break;
				}
				case CMD17: {
					// READ_SINGLE_BLOCK
					uint32_t lba = (rxbuf[1] << 24) | (rxbuf[2] << 16) | (rxbuf[3] << 8) | rxbuf[4];
					static uint8_t read_block_response[2 + 512 + 2];
					read_block_response[0] = 0;
					read_block_response[1] = 0xFE;
#ifdef VERBOSE
					printf("*** SD Reading LBA %d\n", lba);
#endif
					if ((Sint64)lba * 512 >= x16size(sdcard_file)) {
						read_block_response[1] = 0x08; // out of range
						response_length = 2;
					} else {
						x16seek(sdcard_file, (Sint64)lba * 512, XSEEK_SET);
						int bytes_read = x16read(sdcard_file, &read_block_response[2], 1, 512);
						if (bytes_read != 512) {
							printf("Warning: short read!\n");
						}

						response = read_block_response;
						response_length = 2 + 512 + 2;
					}
					break;
				}

				case CMD24: {
					// WRITE_BLOCK
					lba = (rxbuf[1] << 24) | (rxbuf[2] << 16) | (rxbuf[3] << 8) | rxbuf[4];
					if (rxbuf_idx > 4 && (Sint64)lba * 512 >= x16size(sdcard_file)) {
						static uint8_t bad_lba[2] = {0x00, 0x08};
						response = bad_lba;
						response_length = 2;
					} else {
						set_response_r1();
					}
					break;
				}

				case CMD55: {
					// APP_CMD: Next command is an application specific command
					is_acmd = true;
					set_response_r1();
					break;
				}

				case CMD58: {
					// READ_OCR: Read the OCR register of the card
					set_response_r3();
					break;
				}

				default: {
					set_response_r1();
					break;
				}
			}
			response_counter = 0;

#if defined(VERBOSE) && VERBOSE >= 2
			for (int i = 0; i < (response_length < 16 ? response_length : 16); i++) {
				printf(" %02X", response[i]);
			}
			printf("\n");
#endif

		} else if (rxbuf_idx == 515) {
			rxbuf_idx = 0;
			// Check for 'start block' byte
			if (last_cmd == CMD24 && rxbuf[0] == 0xFE) {
#ifdef VERBOSE
				printf("*** SD Writing LBA %d\n", lba);
#endif
				if ((Sint64)lba * 512 >= x16size(sdcard_file)) {
					// do nothing?
				} else {
					x16seek(sdcard_file, (Sint64)lba * 512, XSEEK_SET);
					int bytes_written = x16write(sdcard_file, rxbuf + 1, 1, 512);
					if (bytes_written != 512) {
						printf("Warning: short write!\n");
					}
				}
			}
		}
	}
	return outbyte;
}
