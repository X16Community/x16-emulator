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
//		Disassemble a single 65C02/65C816 instruction into buffer. Returns the length of the
//		instruction in total in bytes.
//
// *******************************************************************************************

int disasm(uint16_t pc, uint8_t *RAM, char *line, unsigned int max_line, int16_t bank, uint8_t implied_status, int32_t *eff_addr) {
	uint8_t opcode = debug_read6502(pc, bank);
	const char *mnemonic = regs.is65c816 ? mnemonics_c816[opcode] : mnemonics_c02[opcode];

	*eff_addr = -1;

	int isZprel = 0;
	int isRel16 = 0;
	int isYrel = 0;
	int isXrel = 0;
	int isIndirect = 0;
	int isBlockMove = 0;
	int isImmediate = 0;
	int isStackRel = 0;

	if (regs.is65c816) {
		// Immediate opcodes (65C816)
		switch (opcode) {
			case 0xC2: // rep #imm8
			case 0xE2: // sep #imm8
				isImmediate = 1;
				;;
			default:
				;;
		}

		// X relative opcodes (65C816)
		switch (opcode) {
			case 0x1F: // ora long,x
			case 0x3F: // and long,x
			case 0x5F: // eor long,x
			case 0x7F: // adc long,x
			case 0x9F: // sta long,x
			case 0xBF: // lda long,x
			case 0xDF: // cmp long,x
			case 0xFC: // jsr (abs,x)
			case 0xFF: // sbc long,x
				isXrel = 1;
				;;
			default:
				;;
		}

		// PER and BRL
		switch (opcode) {
			case 0x62: // per rel16
			case 0x82: // brl rel16
				isRel16 = 1;
			default:
				;;
		}

		// Y relative opcodes (65C816)
		switch (opcode) {
			case 0x13: // ora (zp,S),Y
			case 0x17: // ora [zp],y
			case 0x33: // and (zp,S),Y
			case 0x37: // and [zp],y
			case 0x53: // eor (zp,S),Y
			case 0x57: // eor [zp],y
			case 0x73: // adc (zp,S),Y
			case 0x77: // adc [zp],y
			case 0x93: // sta (zp,S),Y
			case 0x97: // sta [zp],y
			case 0xB3: // lda (zp,S),Y
			case 0xB7: // lda [zp],y
			case 0xD3: // cmp (zp,S),Y
			case 0xD7: // cmp [zp],y
			case 0xF3: // sbc (zp,S),Y
			case 0xF7: // sbc [zp],y
				isYrel = 1;
				;;
			default:
				;;
		}

		// indirect opcodes (65C816)
		switch (opcode) {
			case 0x07: // ora [zp]
			case 0x13: // ora (zp,S),Y
			case 0x17: // ora [zp],y
			case 0x27: // and [zp]
			case 0x33: // and (zp,S),Y
			case 0x37: // and [zp],y
			case 0x47: // eor [zp]
			case 0x53: // eor (zp,S),Y
			case 0x57: // eor [zp],y
			case 0x67: // adc [zp]
			case 0x73: // adc (zp,S),Y
			case 0x77: // adc [zp],y
			case 0x87: // sta [zp]
			case 0x93: // sta (zp,S),Y
			case 0x97: // sta [zp],y
			case 0xA7: // lda [zp]
			case 0xB3: // lda (zp,S),Y
			case 0xB7: // lda [zp],y
			case 0xC7: // cmp [zp]
			case 0xD3: // cmp (zp,S),Y
			case 0xD4: // pei (zp)
			case 0xD7: // cmp [zp],y
			case 0xDC: // jmp [abs]
			case 0xE7: // sbc [zp]
			case 0xF3: // sbc (zp,S),Y
			case 0xF7: // sbc [zp],y
			case 0xFC: // jsr (abs,x)
				isIndirect = 1;
				;;
			default:
				;;
		}

		// stack-relative opcodes (65C816)
		isStackRel = (opcode & 0x0F) == 0x03;

		// block move (MVN and MVP)
		isBlockMove = opcode == 0x44 || opcode == 0x54;

	} else {
		// BBRx and BBSx
		isZprel = (opcode & 0x0F) == 0x0F;

	}

	//
	//		The following cases are common to both 65C816 and 65C02
	//

	//
	//      X relative opcodes (including indirect/indexed)
	//
	switch (opcode) {
		case 0x01: // ora (zp,x)
		case 0x15: // ora zp,x
		case 0x16: // asl zp,x
		case 0x1D: // ora abs,x
		case 0x1E: // asl abs,x
		case 0x21: // and (zp,x)
		case 0x34: // bit zp,x
		case 0x35: // and zp,x
		case 0x36: // rol zp,x
		case 0x3C: // bit abs,x
		case 0x3D: // rol abs,x
		case 0x41: // eor (zp,x)
		case 0x55: // eor zp,x
		case 0x56: // lsr zp,x
		case 0x5d: // eor abs,x
		case 0x5e: // lsr abs,x
		case 0x61: // adc (zp,x)
		case 0x74: // stz zp,x
		case 0x75: // adc zp,x
		case 0x76: // ror zp,x
		case 0x7C: // jmp (abs,x)
		case 0x7D: // adc abs,x
		case 0x7e: // ror abs,x
		case 0x81: // sta (zp,x)
		case 0x94: // sty zp,x
		case 0x95: // sta zp,x
		case 0x9D: // sta abs,x
		case 0x9E: // stz abs,x
		case 0xA1: // lda (zp,x)
		case 0xB4: // ldy zp,x
		case 0xB5: // lda zp,x
		case 0xBC: // ldy abs,x
		case 0xBD: // lda abs,x
		case 0xC1: // cmp (zp,x)
		case 0xD5: // cmp zp,x
		case 0xD6: // dec zp,x
		case 0xDD: // cmp abs,x
		case 0xDE: // dec abs,x
		case 0xD1: // sbc (zp,x)
		case 0xF5: // sbc zp,x
		case 0xF6: // inc zp,x
		case 0xFC: // jsr (abs,x)
		case 0xFD: // sbc abs,x
		case 0xFE: // inc abs,x
			isXrel = 1;
			;;
		default:
			;;
	}

	//
	//		Test for branches, relative address. These are BRA ($80) and
	//		$10,$30,$50,$70,$90,$B0,$D0,$F0.
	//
	int isRel8 = (opcode == 0x80) || ((opcode & 0x1F) == 0x10);

	// is opcode immediate?
	switch (opcode) {
		case 0x09: // ora #imm
		case 0x29: // and #imm
		case 0x49: // eor #imm
		case 0x69: // adc #imm
		case 0x89: // bit #imm
		case 0xA0: // ldy #imm
		case 0xA2: // ldx #imm
		case 0xA9: // lda #imm
		case 0xC0: // cpy #imm
		case 0xC9: // cmp #imm
		case 0xE0: // cpx #imm
		case 0xE9: // sbc #imm
			isImmediate = 1;
			;;
		default:
			;;
	}

	//      Y relative opcodes (including indirect/indexed)
	switch (opcode) {
		case 0x11: // ora (zp),y
		case 0x19: // ora abs,y
		case 0x31: // and (zp),y
		case 0x39: // and abs,y
		case 0x51: // eor (zp),y
		case 0x59: // eor abs,y
		case 0x71: // adc (zp),y
		case 0x79: // adc abs,y
		case 0x91: // sta (zp),y
		case 0x96: // stx zp,y
		case 0x99: // sta abs,y
		case 0xB1: // lda (zp),y
		case 0xB6: // ldx zp,y
		case 0xB9: // lda abs,y
		case 0xBE: // ldx abs,y
		case 0xD1: // cmp (zp),y
		case 0xD9: // cmp abs,y
		case 0xF1: // sbc (zp),y
		case 0xF9: // sbc abs,y
			isYrel = 1;
			;;
		default:
			;;
	}

	//	All indirect opcodes
	
	switch (opcode) {
		case 0x01: // ora (zp,x)
		case 0x11: // ora (zp),y
		case 0x12: // ora (zp)
		case 0x21: // and (zp,x)
		case 0x31: // and (zp),y
		case 0x32: // and (zp)
		case 0x41: // eor (zp,x)
		case 0x51: // eor (zp),y
		case 0x52: // eor (zp)
		case 0x61: // adc (zp,x)
		case 0x6C: // jmp (abs)
		case 0x71: // adc (zp),y
		case 0x72: // adc (zp)
		case 0x7C: // jmp (abs,x)
		case 0x81: // sta (zp,x)
		case 0x91: // sta (zp),y
		case 0x92: // sta (zp)
		case 0xA1: // lda (zp,x)
		case 0xB1: // lda (zp),y
		case 0xB2: // lda (zp)
		case 0xC1: // cmp (zp,x)
		case 0xD1: // cmp (zp),y
		case 0xD2: // cmp (zp)
		case 0xE1: // sbc (zp,x)
		case 0xF1: // sbc (zp),y
		case 0xF2: // sbc (zp)
			isIndirect = 1;
			;;
		default:
			;;
	}

	// BRK and COP (works with 65C02 as well since it's a nop #imm8)
	int isBrkOrCop = opcode == 0x00 || opcode == 0x02;

	int length = 1;

	char *where = strstr(mnemonic, "%%0%hhux");
	if (where) {
		int isImmediateIndex = (opcode == 0xa0) || (opcode == 0xa2) || (opcode == 0xc0) || (opcode == 0xe0);
		int len = snprintf(line, max_line, mnemonic, (implied_status & (isImmediateIndex ? FLAG_INDEX_WIDTH : FLAG_MEMORY_WIDTH)) ? 2 : 4);
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
		snprintf(line, max_line, mnemonic, debug_read6502(pc + 1, bank), debug_read6502(pc + 2, bank));
		length = 3;
		if (regs.c != 0xFFFF) *eff_addr = regs.y; // We can have only one effective address, so we're choosing the destination
	}
	else if (isZprel) {
		snprintf(line, max_line, mnemonic, debug_read6502(pc + 1, bank), pc + 3 + (int8_t)debug_read6502(pc + 2, bank));
		length = 3;
	}
	else if (isRel16) {
		snprintf(line, max_line, mnemonic, (pc + 3 + (int16_t)(debug_read6502(pc + 1, bank) | (debug_read6502(pc + 2, bank) << 8))) & 0xffff);
		length = 3;
	}
	else if (isBrkOrCop) {
		snprintf(line, max_line, mnemonic, debug_read6502(pc + 1, bank));
		length = 2;
	} else {
		if (strstr(line, "%02x")) {
			length = 2;
			if (isRel8) {
				snprintf(line, max_line, mnemonic, pc + 2 + (int8_t)debug_read6502(pc + 1, bank));
			} else {
				snprintf(line, max_line, mnemonic, debug_read6502(pc + 1, bank));
				if (isStackRel) {
					uint16_t ptr = regs.sp + debug_read6502(pc + 1, bank);
					uint8_t ind_bank = ptr < 0xc000 ? memory_get_ram_bank() : memory_get_rom_bank();
					if (isIndirect && isYrel) {
						*eff_addr = (debug_read6502(ptr, ind_bank) | (debug_read6502(ptr + 1, ind_bank) << 8)) + regs.y;
					} else {
						*eff_addr = ptr;
					}
				} else if (isIndirect) {
					uint16_t ptr = debug_read6502(pc + 1, bank);
					if (isXrel)
						ptr += regs.x;
					*eff_addr = debug_read6502(ptr, bank) | (debug_read6502(ptr + 1, bank) << 8);
					if (isYrel)
						*eff_addr += regs.y;
				} else if (!isImmediate) {
					*eff_addr = debug_read6502(pc + 1, bank);
					if (isXrel)
						*eff_addr += regs.x;
					if (isYrel)
						*eff_addr += regs.y;
				}
			}
		}
		if (strstr(line, "%04x")) {
			length = 3;
			snprintf(line, max_line, mnemonic, debug_read6502(pc + 1, bank) | debug_read6502(pc + 2, bank) << 8);
			if (isIndirect) {
				uint16_t ptr = debug_read6502(pc + 1, bank) | (debug_read6502(pc + 2, bank) << 8);
				if (isXrel)
					ptr += regs.x;
				uint8_t ind_bank = ptr < 0xc000 ? memory_get_ram_bank() : memory_get_rom_bank();
				*eff_addr = debug_read6502(ptr, ind_bank) | (debug_read6502(ptr + 1, ind_bank) << 8);
				if (isYrel)
					*eff_addr += regs.y;
			} else if (!isImmediate) {
				*eff_addr = debug_read6502(pc + 1, bank) | (debug_read6502(pc + 2, bank) << 8);
				if (isXrel)
					*eff_addr += regs.x;
				if (isYrel)
					*eff_addr += regs.y;
			}
		}
		if (strstr(line, "%06x")) {
			length = 4;
			snprintf(line, max_line, mnemonic, debug_read6502(pc + 1, bank) | debug_read6502(pc + 2, bank) << 8 | debug_read6502(pc + 3, bank) << 16);
			if (isIndirect) {
				uint16_t ptr = debug_read6502(pc + 1, bank) | (debug_read6502(pc + 2, bank) << 8);
				if (isXrel)
					ptr += regs.x;
				uint8_t ind_bank = ptr < 0xc000 ? memory_get_ram_bank() : memory_get_rom_bank();
				*eff_addr = debug_read6502(ptr, ind_bank) | (debug_read6502(ptr + 1, ind_bank) << 8);
				if (isYrel)
					*eff_addr += regs.y;
			} else {
				*eff_addr = debug_read6502(pc + 1, bank) | (debug_read6502(pc + 2, bank) << 8);
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

	if (*eff_addr >= 0x10000) {
		*eff_addr &= 0xffff;
	}

	return length;
}
