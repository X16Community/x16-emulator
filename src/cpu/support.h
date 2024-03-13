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

#define overflowcalc8(n, m, o) { /* n = result, m = accumulator, o = memory */ \
    if (((n) ^ (m)) & ((n) ^ (o)) & 0x80) setoverflow();\
        else clearoverflow();\
}

#define overflowcalc16(n, m, o) { /* n = result, m = accumulator, o = memory */ \
    if (((n) ^ (m)) & ((n) ^ (o)) & 0x8000) setoverflow();\
        else clearoverflow();\
}

#define index_16bit() (regs.is65c816 && !(regs.status & FLAG_INDEX_WIDTH))
#define memory_16bit() (regs.is65c816 && !(regs.status & FLAG_MEMORY_WIDTH))
#define acc_for_mode() (memory_16bit() ? regs.c : ((uint16_t) regs.a))


#define saveaccum(n) (memory_16bit() ? (regs.c = (n)) : (regs.a = (uint8_t)((n) & 0x00FF)))

//a few general functions used by various other functions

uint16_t add_wrap_at_page_boundary(uint16_t value, uint8_t add) {
    if (regs.e) {
        return (value & 0xFF00) | ((uint16_t) (((uint8_t) (value & 0x00FF)) + add) & 0x00FF);
    } else {
        return value + add;
    }
}

uint16_t subtract_wrap_at_page_boundary(uint16_t value, uint8_t subtract) {
    if (regs.e) {
        return (value & 0xFF00) | ((uint16_t) (((uint8_t) (value & 0x00FF)) - subtract) & 0x00FF);
    } else {
        return value - subtract;
    }
}

void increment_wrap_at_page_boundary(uint16_t *value) {
    if (regs.e) {
        *value = (*value & 0xFF00) | ((uint16_t) (((uint8_t) (*value & 0x00FF)) + 1) & 0x00FF);
    } else {
        (*value)++;
    }
}

void decrement_wrap_at_page_boundary(uint16_t *value) {
    if (regs.e) {
        *value = (*value & 0xFF00) | ((uint16_t) (((uint8_t) (*value & 0x00FF)) - 1) & 0x00FF);
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
    regs.x = 0;
    regs.y = 0;
    regs.dp = 0;
    regs.sp = 0x1FD;
    regs.e = 1;
    regs.k = 0;
    regs.db = 0;
    if (c816) {
        regs.status |= FLAG_INDEX_WIDTH | FLAG_MEMORY_WIDTH;
        regs.is65c816 = true;
        ticktable = ticktable_c816;
        optable = optable_c816;
        addrtable = addrtable_c816;
    } else {
        regs.status |= FLAG_CONSTANT;
        regs.is65c816 = false;
        ticktable = ticktable_c02;
        optable = optable_c02;
        addrtable = addrtable_c02;
    }
    setinterrupt();
    cleardecimal();
    waiting = 0;
}

enum InterruptType {
    INT_COP = 0x4,
    INT_BRK = 0x6,
    INT_NMI = 0xA,
    INT_IRQ = 0xE
};

void interrupt6502(enum InterruptType vector) {
    if (!regs.e) {
        push8(regs.k);
    }

    regs.k = 0; // also in emulated mode

    push16(regs.pc);

    if (regs.e) {
        if (vector == INT_BRK) {
            push8(regs.status | FLAG_BREAK);
            vector = INT_IRQ;
        } else {
            push8(regs.status & ~FLAG_BREAK);
        }
    } else {
        push8(regs.status);
    }

    setinterrupt();
    cleardecimal();
    vp6502();

    uint16_t vector_address = (regs.e ? 0xFFF0 : 0xFFE0) + (uint8_t) vector;
    regs.pc = (uint16_t) read6502(vector_address) | ((uint16_t) read6502(vector_address + 1) << 8);

    clockticks6502 += 7; // consumed by CPU to process interrupt
}
