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
#define zerocalc(n, use16Bit) {\
    if (use16Bit) \
        if ((n) & 0xFFFF) clearzero();\
            else setzero();\
    else if ((n) & 0x00FF) clearzero();\
        else setzero();\
}

#define signcalc(n, use16Bit) {\
    if (use16Bit) \
        if ((n) & 0x8000) setsign();\
            else clearsign();\
    else if ((n) & 0x0080) setsign();\
        else clearsign();\
}

#define carrycalc(n, use16Bit) {\
    if (use16Bit) \
        if ((n) & 0x10000) setcarry();\
            else clearcarry();\
    else if ((n) & 0x0100) setcarry();\
        else clearcarry();\
}

#define overflowcalc(n, m, o) { /* n = result, m = accumulator, o = memory */ \
    if (((n) ^ (uint16_t)(m)) & ((n) ^ (o)) & (memory_16bit() ? 0x8000 : 0x0080)) setoverflow();\
        else clearoverflow();\
}

#define index_16bit() (regs.is65c816 && !(regs.status & FLAG_INDEX_WIDTH))
#define memory_16bit() (regs.is65c816 && !(regs.status & FLAG_MEMORY_WIDTH))
#define acc_for_mode() (memory_16bit() ? regs.c : ((uint16_t) regs.a))


#define saveaccum(n) (memory_16bit() ? (regs.c = (n)) : (regs.a = (uint8_t)((n) & 0x00FF)))

//a few general functions used by various other functions

uint16_t add_wrap_at_page_boundary(uint16_t value, uint8_t add) {
    if (regs.e) {
        return (value & 0xFF00) | ((uint16_t) ((uint8_t) (value & 0x00FF)) + add);
    } else {
        return value + add;
    }
}

uint16_t subtract_wrap_at_page_boundary(uint16_t value, uint8_t subtract) {
    if (regs.e) {
        return (value & 0xFF00) | ((uint16_t) ((uint8_t) (value & 0x00FF)) - subtract);
    } else {
        return value - subtract;
    }
}

void increment_wrap_at_page_boundary(uint16_t *value) {
    if (regs.e) {
        *value = (*value & 0xFF00) | ((uint16_t) ((uint8_t) (*value & 0x00FF)) + 1);
    } else {
        (*value)++;
    }
}

void decrement_wrap_at_page_boundary(uint16_t *value) {
    if (regs.e) {
        *value = (*value & 0xFF00) | ((uint16_t) ((uint8_t) (*value & 0x00FF)) - 1);
    } else {
        (*value)--;
    }
}

uint16_t direct_page_add(uint16_t offset) {
    if (regs.e && (regs.dp & 0x00FF) == 0) {
        return (regs.dp & 0xFF00) | ((uint16_t) ((uint8_t) (regs.dp & 0x00FF)) + (offset & 0xFF));
    } else {
        return regs.dp + offset;
    }
}

#define incsp() increment_wrap_at_page_boundary(&regs.sp)
#define decsp() decrement_wrap_at_page_boundary(&regs.sp)

void push16(uint16_t pushval) {
    write6502(regs.sp, (pushval >> 8) & 0xFF);
    decsp();
    write6502(regs.sp, pushval & 0xFF);
    decsp();
}

void push8(uint8_t pushval) {
    write6502(regs.sp, pushval);
    decsp();
}

uint16_t pull16() {
    incsp();
    uint16_t temp16 = read6502(regs.sp);
    incsp();
    temp16 |= (uint16_t) read6502(regs.sp) << 8;
    return temp16;
}

uint8_t pull8() {
    incsp();
    uint8_t value = read6502(regs.sp);
    return value;
}

void reset6502(bool c816) {
    regs.pc = (uint16_t)read6502(0xFFFC) | ((uint16_t)read6502(0xFFFD) << 8);
    regs.c = 0;
    regs.xw = 0;
    regs.yw = 0;
    regs.dp = 0;
    regs.sp = 0x1FD;
    regs.e = 1;
    if (c816) {
        regs.status |= FLAG_INDEX_WIDTH | FLAG_MEMORY_WIDTH;
        regs.is65c816 = true;
    } else {
        regs.status |= FLAG_CONSTANT;
        regs.is65c816 = false;
    }
    setinterrupt();
    cleardecimal();
    waiting = 0;
}
