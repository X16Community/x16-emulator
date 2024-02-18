
#ifndef _REGISTERS_H_
#define _REGISTERS_H_

#include <stdint.h>
#include <stdbool.h>

#define FLAG_CARRY     0x01
#define FLAG_ZERO      0x02
#define FLAG_INTERRUPT 0x04
#define FLAG_DECIMAL   0x08
#define FLAG_INDEX_WIDTH 0x10
#define FLAG_MEMORY_WIDTH  0x20
#define FLAG_OVERFLOW  0x40
#define FLAG_SIGN      0x80

// For 65C02
#define FLAG_BREAK FLAG_INDEX_WIDTH
#define FLAG_CONSTANT FLAG_MEMORY_WIDTH

//6502 CPU registers

struct regs
{
    union
    {
        struct
        {
            uint8_t a;
            uint8_t b;
        };

        uint16_t c;
    };

    union
    {
        struct
        {
            uint8_t xl;
            uint8_t xh;
        };
        uint16_t x;
    };

    union
    {
        struct
        {
            uint8_t yl;
            uint8_t yh;
        };
        uint16_t y;
    };

    uint16_t dp;
    uint16_t sp;

    uint8_t db;
    uint16_t pc;
    uint8_t k;

    uint8_t status;
    uint8_t e;

    bool is65c816;
};

void increment_wrap_at_page_boundary(uint16_t *value);
void decrement_wrap_at_page_boundary(uint16_t *value);
uint16_t direct_page_add(uint16_t offset);

#endif
