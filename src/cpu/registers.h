
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

    uint8_t dp;

    union
    {
        struct
        {
            uint8_t spl;
            uint8_t sph;
        };
        uint16_t sp;
    };

    uint8_t db;
    uint16_t pc;
    uint8_t pb;

    uint8_t status;
    uint8_t e;
};

#endif
