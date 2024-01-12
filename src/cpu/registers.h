
#ifndef _REGISTERS_H_
#define _REGISTERS_H_

#include <stdint.h>

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
            uint8_t x;
            uint8_t xh;
        };
        uint16_t xw;
    };

    union
    {
        struct
        {
            uint8_t y;
            uint8_t yh;
        };
        uint16_t yw;
    };

    uint16_t dp;
    uint16_t sp;

    uint8_t db;
    uint16_t pc;
    uint8_t pb;

    uint8_t status;
    uint8_t e;
};

void increment_wrap_at_page_boundary(uint16_t *value);
void decrement_wrap_at_page_boundary(uint16_t *value);
uint16_t direct_page_add(uint16_t offset);

#endif
