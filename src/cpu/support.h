/*

						Extracted from original single fake6502.c file

*/


//flag modifier macros
#define setcarry() regs.status |= FLAG_CARRY
#define clearcarry() regs.status &= (~FLAG_CARRY)
#define setzero() regs.status |= FLAG_ZERO
#define clearzero() regs.status &= (~FLAG_ZERO)
#define setinterrupt() regs.status |= FLAG_INTERRUPT
#define clearinterrupt() regs.status &= (~FLAG_INTERRUPT)
#define setdecimal() regs.status |= FLAG_DECIMAL
#define cleardecimal() regs.status &= (~FLAG_DECIMAL)
#define setoverflow() regs.status |= FLAG_OVERFLOW
#define clearoverflow() regs.status &= (~FLAG_OVERFLOW)
#define setsign() regs.status |= FLAG_SIGN
#define clearsign() regs.status &= (~FLAG_SIGN)


//flag calculation macros
#define zerocalc(n) {\
    if ((n) & 0x00FF) clearzero();\
        else setzero();\
}

#define signcalc(n) {\
    if ((n) & 0x0080) setsign();\
        else clearsign();\
}

#define carrycalc(n) {\
    if ((n) & 0xFF00) setcarry();\
        else clearcarry();\
}

#define overflowcalc(n, m, o) { /* n = result, m = accumulator, o = memory */ \
    if (((n) ^ (uint16_t)(m)) & ((n) ^ (o)) & 0x0080) setoverflow();\
        else clearoverflow();\
}

#define index_16bit() (!(regs.status & FLAG_INDEX_WIDTH))
#define memory_16bit() (!(regs.status & FLAG_MEMORY_WIDTH))
#define acc_for_mode() (memory_16bit() ? regs.c : ((uint16_t) regs.a))


#define saveaccum(n) (memory_16bit() ? (regs.c = (n)) : (regs.a = (uint8_t)((n) & 0x00FF)))

//a few general functions used by various other functions
void push16(uint16_t pushval) {
    write6502(regs.sp, (pushval >> 8) & 0xFF);
    regs.spl--;
    write6502(regs.sp, pushval & 0xFF);
    regs.spl--;
}

void push8(uint8_t pushval) {
    write6502(regs.sp, pushval);
    regs.spl--;
}

uint16_t pull16() {
    regs.spl++;
    uint16_t temp16 = read6502(regs.sp);
    regs.spl++;
    temp16 |= (uint16_t) read6502(regs.sp) << 8;
    return temp16;
}

uint8_t pull8() {
    regs.spl++;
    uint8_t value = read6502(regs.sp);
    return value;
}

void reset6502() {
    regs.pc = (uint16_t)read6502(0xFFFC) | ((uint16_t)read6502(0xFFFD) << 8);
    regs.c = 0;
    regs.xw = 0;
    regs.yw = 0;
    regs.sp = 0x1FD;
    regs.status |= FLAG_INDEX_WIDTH | FLAG_MEMORY_WIDTH;
    regs.e = 1;
    setinterrupt();
    cleardecimal();
    waiting = 0;
}
