#ifndef ISO_8859_15_H
#define ISO_8859_15_H

#include <stdint.h>

uint8_t iso8859_15_from_unicode(uint32_t c);
uint32_t unicode_from_iso8859_15(uint8_t c);
void print_iso8859_15_char(char c);
#endif
