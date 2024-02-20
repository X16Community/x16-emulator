// Commander X16 Emulator
// Copyright (c) 2019 Michael Steil
// All rights reserved. License: 2-clause BSD

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include "memory.h"
#include "glue.h"

#include "cpu/mnemonics.h"				// Automatically generated mnemonic table.

// *******************************************************************************************
//
//		Disassemble a single 65C02 instruction into buffer. Returns the length of the
//		instruction in total in bytes.
//
// *******************************************************************************************

int disasm(uint16_t pc, uint8_t *RAM, char *line, unsigned int max_line, bool debugOn, uint8_t bank, int32_t *eff_addr) {
	uint8_t opcode = real_read6502(pc, debugOn, bank);
	const char *mnemonic = regs.is65c816 ? mnemonics_c816[opcode] : mnemonics_c02[opcode];

	*eff_addr = -1;

	//
	//		Test for branches, relative address. These are BRA ($80) and
	//		$10,$30,$50,$70,$90,$B0,$D0,$F0.
	//
	//
	int isBranch = (opcode == 0x80) || ((opcode & 0x1F) == 0x10);
	//
	//		Ditto bbr and bbs, the "zero-page, relative" ops.
	//		$0F,$1F,$2F,$3F,$4F,$5F,$6F,$7F,$8F,$9F,$AF,$BF,$CF,$DF,$EF,$FF
	//
	int isZprel  = (opcode & 0x0F) == 0x0F;
	//
	//		PER is a rel16 opcode
	//
	int isRel16 = (opcode == 0x62);
	//
	//      X relative opcodes (including indirect/indexed)
	//
	int isXrel = 0;
	switch (opcode) {
		case 0x01: // ora (zp,x)
		case 0x15: // ora zp,x
		case 0x16: // asl zp,x
		case 0x1d: // ora abs,x
		case 0x1f: // ora long,x
		case 0x1e: // asl abs,x
		case 0x21: // and (zp,x)
		case 0x34: // bit zp,x
		case 0x35: // and zp,x
		case 0x36: // rol zp,x
		case 0x3c: // bit abs,x
		case 0x3d: // rol abs,x
		case 0x41: // eor (zp,x)
		case 0x55: // eor zp,x
		case 0x56: // lsr zp,x
		case 0x5d: // eor abs,x
		case 0x5f: // eor long,x
		case 0x5e: // lsr abs,x
		case 0x61: // adc (zp,x)
		case 0x74: // stz zp,x
		case 0x75: // adc zp,x
		case 0x76: // ror zp,x
		case 0x7c: // jmp (abs,x)
		case 0x7d: // adc abs,x
		case 0x7f: // adc long,x
		case 0x7e: // ror abs,x
		case 0x81: // sta (zp,x)
		case 0x94: // sty zp,x
		case 0x95: // sta zp,x
		case 0x9d: // sta abs,x
		case 0x9f: // sta long,x
		case 0x9e: // stz abs,x
		case 0xa1: // lda (zp,x)
		case 0xb4: // ldy zp,x
		case 0xb5: // lda zp,x
		case 0xbc: // ldy abs,x
		case 0xbd: // lda abs,x
		case 0xbf: // lda long,x
		case 0xc1: // cmp (zp,x)
		case 0xd5: // cmp zp,x
		case 0xd6: // dec zp,x
		case 0xdd: // cmp abs,x
		case 0xdf: // cmp long,x
		case 0xde: // dec abs,x
		case 0xe1: // sbc (zp,x)
		case 0xf5: // sbc zp,x
		case 0xf6: // inc zp,x
		case 0xfc: // jsr (abs,x)
		case 0xfd: // sbc abs,x
		case 0xfe: // inc abs,x
		case 0xff: // sbc long,x
			isXrel = 1;
			;;
		default:
			;;
	} 

	// is opcode immedaite?

	int isImmediate = (((opcode & 0x1f) == 0x09) || opcode == 0xa0 || opcode == 0xa2 || opcode == 0xc0 || opcode == 0xe0);

	//
	//      Y relative opcodes (including indirect/indexed)
	//	$x1, $x7 and $x9 (for odd values of x), as well as $96 and $B6
	int isYrel = (((opcode & 0x17) == 0x11) || ((opcode & 0x17) == 0x17) || opcode == 0x96 || opcode == 0xb6);

	//
	//      indirect
	//  $x1 and $xf ($x2 where x is odd)
	//  as well as $6C, $7C and $FC
	int isIndirect = (((opcode & 0x0f) == 0x01) || ((opcode & 0x1f) == 0x12) || opcode == 0x6c || opcode == 0x7c || opcode == 0xfc);

	//
	//      block move (MVN and MVP)
	int isBlockMove = opcode == 0x44 || opcode == 0x54;

	int length   = 1;

	char *where = strstr(mnemonic, "%%0%hhux");
	if (where) {
		int isImmediateIndex = (opcode == 0xa0) || (opcode == 0xa2) || (opcode == 0xc0) || (opcode == 0xe0);
		int len = snprintf(line, max_line, mnemonic, (regs.status & (isImmediateIndex ? FLAG_INDEX_WIDTH : FLAG_MEMORY_WIDTH)) ? 2 : 4);
		if (len == -1) {
			return 0;
		}

		mnemonic = malloc(len + 1);
		memcpy((char *) mnemonic, line, len + 1);
	}
	else {
		strncpy(line,mnemonic,max_line);
	}

	if (isBlockMove) {
		snprintf(line, max_line, mnemonic, real_read6502(pc + 1, debugOn, bank), real_read6502(pc + 2, debugOn, bank));
		length = 3;
	}
	else if (isZprel) {
		snprintf(line, max_line, mnemonic, real_read6502(pc + 1, debugOn, bank), pc + 3 + (int8_t)real_read6502(pc + 2, debugOn, bank));
		length = 3;
	}
	else if (isRel16) {
		snprintf(line, max_line, mnemonic, (pc + 3 + (int16_t)(real_read6502(pc + 1, debugOn, bank) | (real_read6502(pc + 2, debugOn, bank) << 8))) & 0xffff);
		length = 3;
	} else {
		if (strstr(line, "%02x")) {
			length = 2;
			if (isBranch) {
				snprintf(line, max_line, mnemonic, pc + 2 + (int8_t)real_read6502(pc + 1, debugOn, bank));
			} else {
				snprintf(line, max_line, mnemonic, real_read6502(pc + 1, debugOn, bank));
				if (isIndirect) {
					uint16_t ptr = real_read6502(pc + 1, debugOn, bank);
					if (isXrel)
						ptr += regs.x;
					*eff_addr = real_read6502(ptr, debugOn, bank) | (real_read6502(ptr + 1, debugOn, bank) << 8);
					if (isYrel)
						*eff_addr += regs.y;
				} else if (!isImmediate) {
					*eff_addr = real_read6502(pc + 1, debugOn, bank);
					if (isXrel)
						*eff_addr += regs.x;
					if (isYrel)
						*eff_addr += regs.y;
				}
			}
		}
		if (strstr(line, "%04x")) {
			length = 3;
			snprintf(line, max_line, mnemonic, real_read6502(pc + 1, debugOn, bank) | real_read6502(pc + 2, debugOn, bank) << 8);
			if (isIndirect) {
				uint16_t ptr = real_read6502(pc + 1, debugOn, bank) | (real_read6502(pc + 2, debugOn, bank) << 8);
				if (isXrel)
					ptr += regs.x;
				*eff_addr = real_read6502(ptr, debugOn, bank) | (real_read6502(ptr + 1, debugOn, bank) << 8);
				if (isYrel)
					*eff_addr += regs.y;
			} else {
				*eff_addr = real_read6502(pc + 1, debugOn, bank) | (real_read6502(pc + 2, debugOn, bank) << 8);
				if (isXrel)
					*eff_addr += regs.x;
				if (isYrel)
					*eff_addr += regs.y;
			}
		}
		if (strstr(line, "%06x")) {
			length = 4;
			snprintf(line, max_line, mnemonic, real_read6502(pc + 1, debugOn, bank) | real_read6502(pc + 2, debugOn, bank) << 8 | real_read6502(pc + 3, debugOn, bank) << 16);
			if (isIndirect) {
				uint16_t ptr = real_read6502(pc + 1, debugOn, bank) | (real_read6502(pc + 2, debugOn, bank) << 8);
				if (isXrel)
					ptr += regs.x;
				*eff_addr = real_read6502(ptr, debugOn, bank) | (real_read6502(ptr + 1, debugOn, bank) << 8);
				if (isYrel)
					*eff_addr += regs.y;
			} else {
				*eff_addr = real_read6502(pc + 1, debugOn, bank) | (real_read6502(pc + 2, debugOn, bank) << 8);
				if (isXrel)
					*eff_addr += regs.x;
				if (isYrel)
					*eff_addr += regs.y;
			}
		}
		if (opcode == 0x00 || opcode == 0x02) {
			// BRK and COP instructions are 2 bytes long according to WDC datasheet.
			// CPU skips the second byte when it executes a BRK / COP.
			length = 2;
		}
	}

	if (where) {
		free((char *) mnemonic);
	}

	return length;
}
