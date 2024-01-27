/*

						Extracted from original single fake6502.c file

*/
//
//          65C02 changes.
//
//          BRK                 now clears D
//          ADC/SBC             set N and Z in decimal mode. They also set V, but this is
//                              essentially meaningless so this has not been implemented.
//
//
//
//          instruction handler functions
//
static void adc() {
    penaltyop = 1;
    #ifndef NES_CPU
    if (regs.status & FLAG_DECIMAL) {
        uint16_t tmp, tmp2;
        value = getvalue(0);
        tmp = ((uint16_t)regs.a & 0x0F) + (value & 0x0F) + (uint16_t)(regs.status & FLAG_CARRY);
        tmp2 = ((uint16_t)regs.a & 0xF0) + (value & 0xF0);
        if (tmp > 0x09) {
            tmp2 += 0x10;
            tmp += 0x06;
        }
        if (tmp2 > 0x90) {
            tmp2 += 0x60;
        }
        if (tmp2 & 0xFF00) {
            setcarry();
        } else {
            clearcarry();
        }
        result = (tmp & 0x0F) | (tmp2 & 0xF0);

        zerocalc(result, memory_16bit());                /* 65C02 change, Decimal Arithmetic sets NZV */
        signcalc(result, memory_16bit());

        clockticks6502++;
    } else {
    #endif
        value = getvalue(memory_16bit());
        result = acc_for_mode() + value + (uint16_t) (regs.status & FLAG_CARRY);

        carrycalc(result, memory_16bit());
        zerocalc(result, memory_16bit());
        overflowcalc(result, acc_for_mode(), value);
        signcalc(result, memory_16bit());
    #ifndef NES_CPU
    }
    #endif

    saveaccum(result);
}

static void and() {
    penaltyop = 1;
    value = getvalue(memory_16bit());
    result = acc_for_mode() & value;

    zerocalc(result, memory_16bit());
    signcalc(result, memory_16bit());

    saveaccum(result);
}

static void asl() {
    value = getvalue(memory_16bit());
    result = value << 1;

    carrycalc(result, memory_16bit());
    zerocalc(result, memory_16bit());
    signcalc(result, memory_16bit());

    putvalue(result, memory_16bit());
}

static void bcc() {
    if ((regs.status & FLAG_CARRY) == 0) {
        oldpc = regs.pc;
        regs.pc += reladdr;
        if ((oldpc & 0xFF00) != (regs.pc & 0xFF00)) clockticks6502 += 2; //check if jump crossed a page boundary
            else clockticks6502++;
    }
}

static void bcs() {
    if ((regs.status & FLAG_CARRY) == FLAG_CARRY) {
        oldpc = regs.pc;
        regs.pc += reladdr;
        if ((oldpc & 0xFF00) != (regs.pc & 0xFF00)) clockticks6502 += 2; //check if jump crossed a page boundary
            else clockticks6502++;
    }
}

static void beq() {
    if ((regs.status & FLAG_ZERO) == FLAG_ZERO) {
        oldpc = regs.pc;
        regs.pc += reladdr;
        if ((oldpc & 0xFF00) != (regs.pc & 0xFF00)) clockticks6502 += 2; //check if jump crossed a page boundary
            else clockticks6502++;
    }
}

static void bit() {
    value = getvalue(memory_16bit());
    result = acc_for_mode() & value;

    zerocalc(result, memory_16bit());
    // Xark - BUGFIX: 65C02 BIT #$xx only affects Z  See: http://6502.org/tutorials/65c02opcodes.html#2
    if (opcode != 0x89)
    {
        regs.status = (regs.status & 0x3F) | (uint8_t)(value & 0xC0);
    }
}

static void bmi() {
    if ((regs.status & FLAG_SIGN) == FLAG_SIGN) {
        oldpc = regs.pc;
        regs.pc += reladdr;
        if ((oldpc & 0xFF00) != (regs.pc & 0xFF00)) clockticks6502 += 2; //check if jump crossed a page boundary
            else clockticks6502++;
    }
}

static void bne() {
    if ((regs.status & FLAG_ZERO) == 0) {
        oldpc = regs.pc;
        regs.pc += reladdr;
        if ((oldpc & 0xFF00) != (regs.pc & 0xFF00)) clockticks6502 += 2; //check if jump crossed a page boundary
            else clockticks6502++;
    }
}

static void bpl() {
    if ((regs.status & FLAG_SIGN) == 0) {
        oldpc = regs.pc;
        regs.pc += reladdr;
        if ((oldpc & 0xFF00) != (regs.pc & 0xFF00)) clockticks6502 += 2; //check if jump crossed a page boundary
            else clockticks6502++;
    }
}

static void brk() {
    penaltye = 1;
    regs.pc++;

    if (!regs.e) {
        push8(regs.k);
    }

    push16(regs.pc); //push next instruction address onto stack
    push8(regs.status | (regs.e ? FLAG_BREAK : 0)); //push CPU status to stack
    setinterrupt(); //set interrupt flag
    cleardecimal();       // clear decimal flag (65C02 change)
    vp6502();

    if (regs.e) {
        regs.pc = (uint16_t)read6502(0xFFFE) | ((uint16_t)read6502(0xFFFF) << 8);
    } else {
        regs.pc = (uint16_t)read6502(0xFFE6) | ((uint16_t)read6502(0xFFE7) << 8);
    }
}

static void bvc() {
    if ((regs.status & FLAG_OVERFLOW) == 0) {
        oldpc = regs.pc;
        regs.pc += reladdr;
        if ((oldpc & 0xFF00) != (regs.pc & 0xFF00)) clockticks6502 += 2; //check if jump crossed a page boundary
            else clockticks6502++;
    }
}

static void bvs() {
    if ((regs.status & FLAG_OVERFLOW) == FLAG_OVERFLOW) {
        oldpc = regs.pc;
        regs.pc += reladdr;
        if ((oldpc & 0xFF00) != (regs.pc & 0xFF00)) clockticks6502 += 2; //check if jump crossed a page boundary
            else clockticks6502++;
    }
}

static void clc() {
    clearcarry();
}

static void cld() {
    cleardecimal();
}

static void cli() {
    clearinterrupt();
}

static void clv() {
    clearoverflow();
}

static void cmp() {
    penaltyop = 1;
    value = getvalue(memory_16bit());

    if (memory_16bit()) {
        result = regs.c - value;
        if(regs.c >= value) setcarry();
        else clearcarry();
        if (regs.c == value) setzero();
        else clearzero();
    } else {
        result = (uint16_t)regs.a - value;
        if (regs.a >= (uint8_t)(value & 0x00FF)) setcarry();
        else clearcarry();
        if (regs.a == (uint8_t)(value & 0x00FF)) setzero();
        else clearzero();
    }

    signcalc(result, memory_16bit());
}

static void cop() {
    penaltye = 1;
    regs.pc++;

    if (!regs.e) {
        push8(regs.k);
    }

    push16(regs.pc); //push next instruction address onto stack
    push8(regs.status | FLAG_BREAK); //push CPU status to stack
    setinterrupt(); //set interrupt flag
    cleardecimal();       // clear decimal flag (65C02 change)
    vp6502();

    if (regs.e) {
        regs.pc = (uint16_t)read6502(0xFFF4) | ((uint16_t)read6502(0xFFF5) << 8);
    } else {
        regs.pc = (uint16_t)read6502(0xFFE4) | ((uint16_t)read6502(0xFFE5) << 8);
    }
}

static void cpx() {
    value = getvalue(index_16bit());

    if (index_16bit()) {
        result = regs.xw - value;
        if(regs.xw >= value) setcarry();
        else clearcarry();
        if (regs.xw == value) setzero();
        else clearzero();
    } else {
        result = (uint16_t)regs.x - value;
        if (regs.x >= (uint8_t)(value & 0x00FF)) setcarry();
        else clearcarry();
        if (regs.x == (uint8_t)(value & 0x00FF)) setzero();
        else clearzero();
    }
    signcalc(result, index_16bit());
}

static void cpy() {
    value = getvalue(index_16bit());

    if (index_16bit()) {
        result = regs.yw - value;
        if(regs.yw >= value) setcarry();
        else clearcarry();
        if (regs.yw == value) setzero();
        else clearzero();
    } else {
        result = (uint16_t)regs.y - value;
        if (regs.y >= (uint8_t)(value & 0x00FF)) setcarry();
        else clearcarry();
        if (regs.y == (uint8_t)(value & 0x00FF)) setzero();
        else clearzero();
    }
    signcalc(result, index_16bit());
}

static void dec() {
    value = getvalue(memory_16bit());
    result = value - 1;

    zerocalc(result, memory_16bit());
    signcalc(result, memory_16bit());

    putvalue(result, memory_16bit());
}

static void dex() {
    if (index_16bit()) {
        regs.xw--;
        zerocalc(regs.xw, 1);
        signcalc(regs.xw, 1);
    } else {
        regs.x--;
        zerocalc(regs.x, 0);
        signcalc(regs.x, 0);
    }
}

static void dey() {
    if (index_16bit()) {
        regs.yw--;
        zerocalc(regs.yw, 1);
        signcalc(regs.yw, 1);
    } else {
        regs.y--;
        zerocalc(regs.y, 0);
        signcalc(regs.y, 0);
    }
}

static void eor() {
    penaltyop = 1;
    value = getvalue(memory_16bit());
    result = acc_for_mode() ^ value;

    zerocalc(result, memory_16bit());
    signcalc(result, memory_16bit());

    saveaccum(result);
}

static void inc() {
    value = getvalue(memory_16bit());
    result = value + 1;

    zerocalc(result, memory_16bit());
    signcalc(result, memory_16bit());

    putvalue(result, memory_16bit());
}

static void inx() {
    if (index_16bit()) {
        regs.xw++;
        zerocalc(regs.xw, 1);
        signcalc(regs.xw, 1);
    } else {
        regs.x++;
        zerocalc(regs.x, 0);
        signcalc(regs.x, 0);
    }
}

static void iny() {
    if (index_16bit()) {
        regs.yw++;
        zerocalc(regs.yw, 1);
        signcalc(regs.yw, 1);
    } else {
        regs.y++;
        zerocalc(regs.y, 0);
        signcalc(regs.y, 0);
    }
}

static void jmp() {
    regs.pc = ea;
}

static void jsr() {
    push16(regs.pc - 1);
    regs.pc = ea;
}

static void lda() {
    penaltyop = 1;
    penaltym = 1;

    if (memory_16bit()) {
        regs.c = getvalue(1);
        zerocalc(regs.c, 1);
        signcalc(regs.c, 1);
    } else {
        value = getvalue(0);
        regs.a = (uint8_t)(value & 0x00FF);
        zerocalc(regs.a, 0);
        signcalc(regs.a, 0);
    }
}

static void ldx() {
    penaltyop = 1;
    penaltyx = 1;

    if (index_16bit()) {
        regs.xw = getvalue(1);
        zerocalc(regs.xw, 1);
        signcalc(regs.xw, 1);
    } else {
        value = getvalue(0);
        regs.x = (uint8_t)(value & 0x00FF);
        zerocalc(regs.x, 0);
        signcalc(regs.x, 0);
    }
}

static void ldy() {
    penaltyop = 1;
    penaltyx = 1;

    if (index_16bit()) {
        regs.yw = getvalue(1);
        zerocalc(regs.yw, 1);
        signcalc(regs.yw, 1);
    } else {
        regs.y = (uint8_t)(getvalue(0) & 0x00FF);
        zerocalc(regs.y, 0);
        signcalc(regs.y, 0);
    }
}

static void lsr() {
    value = getvalue(memory_16bit());
    result = value >> 1;

    if (value & 1) setcarry();
        else clearcarry();
    zerocalc(result, memory_16bit());
    signcalc(result, memory_16bit());

    putvalue(result, memory_16bit());
}

static void nop() {
    switch (opcode) {
        case 0x1C:
        case 0x3C:
        case 0x5C:
        case 0x7C:
        case 0xDC:
        case 0xFC:
            penaltyop = 1;
            break;
    }
}

static void ora() {
    penaltyop = 1;
    value = getvalue(memory_16bit());
    result = acc_for_mode() | value;

    zerocalc(result, memory_16bit());
    signcalc(result, memory_16bit());

    saveaccum(result);
}

static void pea() {
    push16(getvalue(1));
}

static void pha() {
    if (memory_16bit()) {
        push16(regs.c);
    } else {
        push8(regs.a);
    }
}

static void phb() {
    push8(regs.b);
}

static void phd() {
    push16(regs.dp);
}

static void phk() {
    push8(regs.k);
}

static void php() {
    push8(regs.e ? regs.status | FLAG_BREAK : regs.status);
}

static void pla() {
    if (memory_16bit()) {
        regs.c = pull16();
        zerocalc(regs.c, 1);
        signcalc(regs.c, 1);
    } else {
        regs.a = pull8();
        zerocalc(regs.a, 0);
        signcalc(regs.a, 0);
    }
}

static void plb() {
    regs.b = pull8();
    zerocalc(regs.b, 0);
    signcalc(regs.b, 0);
}

static void pld() {
    regs.dp = pull16();
}

static void plp() {
    regs.status = pull8();
    if (regs.e) {
        regs.status |= FLAG_INDEX_WIDTH | FLAG_MEMORY_WIDTH;
    }
}

static void rep() {
    value = getvalue(0);
    regs.status &= ~(value & 0xFF);

    if (regs.e) {
        regs.status |= FLAG_INDEX_WIDTH | FLAG_MEMORY_WIDTH;
    }
}

static void rol() {
    value = getvalue(memory_16bit());
    result = (value << 1) | (regs.status & FLAG_CARRY);

    carrycalc(result, memory_16bit());
    zerocalc(result, memory_16bit());
    signcalc(result, memory_16bit());

    putvalue(result, memory_16bit());
}

static void ror() {
    value = getvalue(memory_16bit());
    result = (value >> 1) | ((regs.status & FLAG_CARRY) << 7);

    if (value & 1) setcarry();
        else clearcarry();
    zerocalc(result, memory_16bit());
    signcalc(result, memory_16bit());

    putvalue(result, memory_16bit());
}

static void rti() {
    regs.status = pull8();
    value = pull16();
    regs.pc = value;

    if (!regs.e) {
        regs.k = pull8();
        regs.status |= FLAG_INDEX_WIDTH | FLAG_MEMORY_WIDTH;
        regs.xh = 0;
        regs.yh = 0;
    }
}

static void rts() {
    value = pull16();
    regs.pc = value + 1;
}

static void sbc() {
    penaltyop = 1;

    #ifndef NES_CPU
    if (regs.status & FLAG_DECIMAL) {
        value = getvalue(0);
        result = (uint16_t)regs.a - (value & 0x0f) + (regs.status & FLAG_CARRY) - 1;
        if ((result & 0x0f) > (regs.a & 0x0f)) {
            result -= 6;
        }
        result -= (value & 0xf0);
        if ((result & 0xfff0) > ((uint16_t)regs.a & 0xf0)) {
            result -= 0x60;
        }
        if (result <= (uint16_t)regs.a) {
            setcarry();
        } else {
            clearcarry();
        }

        zerocalc(result, memory_16bit());                /* 65C02 change, Decimal Arithmetic sets NZV */
        signcalc(result, memory_16bit());

        clockticks6502++;
    } else {
    #endif

        if (memory_16bit()) {
            value = getvalue(1) ^ 0xFFFF;
            result = regs.c + value + (regs.status & FLAG_CARRY);
        } else {
            value = getvalue(0) ^ 0x00FF;
            result = (uint16_t)regs.a + value + (uint16_t)(regs.status & FLAG_CARRY);
        }

        carrycalc(result, memory_16bit());
        zerocalc(result, memory_16bit());
        overflowcalc(result, acc_for_mode(), value);
        signcalc(result, memory_16bit());
    #ifndef NES_CPU
    }
    #endif

    saveaccum(result);
}

static void sec() {
    setcarry();
}

static void sed() {
    setdecimal();
}

static void sei() {
    setinterrupt();
}

static void sep() {
    regs.status |= getvalue(0) & 0xFF;
    if (regs.e) {
        regs.status |= FLAG_INDEX_WIDTH | FLAG_MEMORY_WIDTH;
    }
    if (ea & FLAG_INDEX_WIDTH) {
        regs.xh = 0;
        regs.yh = 0;
    }
}

static void sta() {
    putvalue(acc_for_mode(), memory_16bit());
}

static void stx() {
    putvalue(index_16bit() ? regs.xw : regs.x, index_16bit());
}

static void sty() {
    putvalue(index_16bit() ? regs.yw : regs.y, index_16bit());
}

static void tax() {
    if (index_16bit()) {
        regs.xw = regs.c; // 16 bits transferred, no matter the state of m
        zerocalc(regs.xw, 1);
        signcalc(regs.xw, 1);
    } else {
        regs.x = (uint8_t)(regs.a & 0x00FF);
        zerocalc(regs.x, 0);
        signcalc(regs.x, 0);
    }
}

static void tay() {
    if (index_16bit()) {
        regs.yw = regs.c; // 16 bits transferred, no matter the state of m
        zerocalc(regs.yw, 1);
        signcalc(regs.yw, 1);
    } else {
        regs.y = (uint8_t)(regs.a & 0x00FF);
        zerocalc(regs.y, 0);
        signcalc(regs.y, 0);
    }
}

static void tcd() {
    regs.dp = regs.c;
    zerocalc(regs.dp, 1);
    signcalc(regs.dp, 1);
}

static void tdc() {
    regs.c = regs.dp;
    zerocalc(regs.c, 1);
    signcalc(regs.c, 1);
}

static void tsx() {
    if (index_16bit()) {
        regs.xw = regs.sp; // 16 bits transferred, no matter the state of m
        zerocalc(regs.xw, 1);
        signcalc(regs.xw, 1);
    } else {
        regs.x = (uint8_t)(regs.sp & 0x00FF);
        regs.xh = 0;
        zerocalc(regs.x, 0);
        signcalc(regs.x, 0);
    }
}

static void txa() {
    if (memory_16bit()) {
        if (index_16bit()) {
            regs.c = regs.xw;
            zerocalc(regs.c, 1);
            signcalc(regs.c, 1);
        } else {
            regs.a = regs.x;
            regs.b = 0;
            zerocalc(regs.a, 0);
            signcalc(regs.a, 0);
        }
    } else {
        regs.a = regs.x;
        zerocalc(regs.a, 0);
        signcalc(regs.a, 0);
    }
}

static void txs() {
    if (regs.e) {
        regs.sp = 0x100 | regs.x;
    } else {
        regs.sp = regs.xw;
    }
}

static void txy() {
    if (index_16bit()) {
        regs.yw = regs.xw;
        zerocalc(regs.yw, 1);
        signcalc(regs.yw, 1);
    } else {
        regs.y = regs.x;
        zerocalc(regs.y, 0);
        signcalc(regs.y, 0);
    }
}

static void tya() {
    if (memory_16bit()) {
        if (index_16bit()) {
            regs.c = regs.yw;
            zerocalc(regs.c, 1);
            signcalc(regs.c, 1);
        } else {
            regs.a = regs.y;
            regs.b = 0;
            zerocalc(regs.a, 0);
            signcalc(regs.a, 0);
        }
    } else {
        regs.a = regs.y;
        zerocalc(regs.a, 0);
        signcalc(regs.a, 0);
    }
}

static void tyx() {
    if (index_16bit()) {
        regs.xw = regs.yw;
        zerocalc(regs.xw, 1);
        signcalc(regs.xw, 1);
    } else {
        regs.x = regs.y;
        zerocalc(regs.x, 0);
        signcalc(regs.x, 0);
    }
}

static void tcs() {
    regs.sp = regs.c;
}

static void tsc() {
    regs.c = regs.sp;
    zerocalc(regs.c, 1);
    signcalc(regs.c, 1);
}

static void mvn() {
    if (regs.c != 0xFFFF) {
        if (index_16bit()) {
            write6502(regs.yw++, read6502(regs.xw++));
        } else {
            write6502(regs.y++, read6502(regs.x++));
        }

        regs.c--;
        regs.pc -= 3;
    }
}

static void mvp() {
    if (regs.c != 0xFFFF) {
        if (index_16bit()) {
            write6502(regs.yw--, read6502(regs.xw--));
        } else {
            write6502(regs.y--, read6502(regs.x--));
        }

        regs.c--;
        regs.pc -= 3;
    }
}

static void wdm() {
}

static void xba() {
    uint8_t tmp = regs.b;
    regs.b = regs.a;
    regs.a = tmp;
    zerocalc(regs.a, 0);
    signcalc(regs.a, 0);
}

static void xce() {
    uint8_t carry = regs.status & FLAG_CARRY;
    regs.status = (regs.status & ~FLAG_CARRY) | (regs.e ? FLAG_CARRY : 0);
    regs.e = carry != 0;

    if (regs.e) {
        regs.status |= FLAG_INDEX_WIDTH | FLAG_MEMORY_WIDTH;
        regs.sp = 0x0100 | (regs.sp & 0x00FF);
        regs.xh = 0x00;
        regs.yh = 0x00;
    }
}
