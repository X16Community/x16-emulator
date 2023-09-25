// Commander X16 Emulator
// Copyright (c) 2019,2022 Michael Steil
// All rights reserved. License: 2-clause BSD

#include <stdint.h>
#include <stdio.h>
#include "utf8_encode.h"

uint8_t
iso8859_15_from_unicode(uint32_t c)
{
	// line feed -> carriage return
	if (c == '\n') {
		return '\r';
	}

	// translate Unicode characters not part of Latin-1 but part of Latin-15
	switch (c) {
		case 0x20ac: // '€'
			return 0xa4;
		case 0x160: // 'Š'
			return 0xa6;
		case 0x161: // 'š'
			return 0xa8;
		case 0x17d: // 'Ž'
			return 0xb4;
		case 0x17e: // 'ž'
			return 0xb8;
		case 0x152: // 'Œ'
			return 0xbc;
		case 0x153: // 'œ'
			return 0xbd;
		case 0x178: // 'Ÿ'
			return 0xbe;
	}

	// remove Unicode characters part of Latin-1 but not part of Latin-15
	switch (c) {
		case 0xa4: // '¤'
		case 0xa6: // '¦'
		case 0xa8: // '¨'
		case 0xb4: // '´'
		case 0xb8: // '¸'
		case 0xbc: // '¼'
		case 0xbd: // '½'
		case 0xbe: // '¾'
			return '?';
	}

	// all other Unicode characters are also unsupported
	if (c >= 256) {
		return '?';
	}

	// everything else is Latin-15 already
	return c;
}

uint32_t
unicode_from_iso8859_15(uint8_t c)
{
	// translate Latin-15 characters not part of Latin-1
	switch (c) {
		case 0xa4:
			return 0x20ac; // '€'
		case 0xa6:
			return 0x160; // 'Š'
		case 0xa8:
			return 0x161; // 'š'
		case 0xb4:
			return 0x17d; // 'Ž'
		case 0xb8:
			return 0x17e; // 'ž'
		case 0xbc:
			return 0x152; // 'Œ'
		case 0xbd:
			return 0x153; // 'œ'
		case 0xbe:
			return 0x178; // 'Ÿ'
		default:
			return c;
	}
}

// converts the character to UTF-8 and prints it
void
print_iso8859_15_char(char c)
{
	char utf8[5];
	utf8_encode(utf8, unicode_from_iso8859_15(c));
	printf("%s", utf8);
}
