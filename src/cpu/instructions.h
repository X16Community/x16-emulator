/*

						Extracted from original single fake6502.c file

*/
//
//          65C02 changes.
//
//          BRK                 now clears D
//          ADC/SBC             set N and Z in decimal mode. They also set V
//
//
//
//          instruction handler functions
//
static void adc() {
    penaltyop = 1;
    if (regs.status & FLAG_DECIMAL) {
        uint16_t tmp, tmp2;
        uint32_t tmpov;
        if (memory_16bit()) {
            uint16_t tmp3;
            uint32_t tmp4;
            value = getvalue(1);
            tmp = (regs.c & 0x000F) + (value & 0x000F) + (uint16_t)(regs.status & FLAG_CARRY);
            tmp2 = (regs.c & 0x00F0) + (value & 0x00F0);
            tmp3 = (regs.c & 0x0F00) + (value & 0x0F00);
            tmp4 = ((uint32_t)regs.c & 0xF000) + (value & 0xF000);
            if (tmp > 0x0009) {
                tmp2 += 0x0010;
                tmp += 0x0006;
            }
            if (tmp2 > 0x0090) {
                tmp3 += 0x0100;
                tmp2 += 0x0060;
            }
            if (tmp3 > 0x0900) {
                tmp4 += 0x1000;
                tmp3 += 0x0600;
            }
            tmpov = tmp4;
            if (tmp4 > 0x9000) {
                tmp4 += 0x6000;
            }
            if (tmp4 & 0xFFFF0000) {
                setcarry();
            } else {
                clearcarry();
            }
            result = (tmp & 0x000F) | (tmp2 & 0x00F0) | (tmp3 & 0x0F00) | (tmp4 & 0xF000);
            uint16_t ovresult = (tmp & 0x000F) | (tmp2 & 0x00F0) | (tmp3 & 0x0F00) | (tmpov & 0xF000);
            overflowcalc16(ovresult, regs.c, value);
        } else {
            value = getvalue(0);
            tmp = ((uint16_t)regs.a & 0x0F) + (value & 0x0F) + (uint16_t)(regs.status & FLAG_CARRY);
            tmp2 = ((uint16_t)regs.a & 0xF0) + (value & 0xF0);
            if (tmp > 0x09) {
                tmp2 += 0x10;
                tmp += 0x06;
            }
            tmpov = tmp2;
            if (tmp2 > 0x90) {
                tmp2 += 0x60;
            }
            if (tmp2 & 0xFF00) {
                setcarry();
            } else {
                clearcarry();
            }
            result = (tmp & 0x0F) | (tmp2 & 0xF0);
            uint8_t ovresult = (tmp & 0x0F) | (tmpov & 0xF0);
            overflowcalc8((uint16_t)ovresult, (uint16_t)regs.a, value);
        }
        clockticks6502 += (uint32_t)(!regs.is65c816);
    } else {
        if (memory_16bit()) {
            value = getvalue(1);
            result = regs.c + value + (uint16_t) (regs.status & FLAG_CARRY);
            overflowcalc16(result, regs.c, value);
            carrycalc(result, 1);
        } else {
            value = getvalue(0);
            result = (uint16_t)regs.a + value + (uint16_t) (regs.status & FLAG_CARRY);
            overflowcalc8(result, (uint16_t)regs.a, value);
            carrycalc(result, 0);
        }
    }

    zerocalc(result, memory_16bit());
    signcalc(result, memory_16bit());

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

static void _do_branch(int condition) {
    if (condition) {
        oldpc = regs.pc;
        regs.pc += reladdr;
        clockticks6502++;
        if ((oldpc & 0xFF00) != (regs.pc & 0xFF00)) //check if jump crossed a page boundary
            penaltye = 1;
    }
}

static void bcc() {
    _do_branch((regs.status & FLAG_CARRY) == 0);
}

static void bcs() {
    _do_branch((regs.status & FLAG_CARRY) == FLAG_CARRY);
}

static void beq() {
    _do_branch((regs.status & FLAG_ZERO) == FLAG_ZERO);
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
    _do_branch((regs.status & FLAG_SIGN) == FLAG_SIGN);
}

static void bne() {
    _do_branch((regs.status & FLAG_ZERO) == 0);
}

static void bpl() {
    _do_branch((regs.status & FLAG_SIGN) == 0);
}

static void brk() {
    penaltyn = 1;
    regs.pc++;

    interrupt6502(INT_BRK);
}

static void brl() {
    regs.pc += reladdr;
}

static void bvc() {
    _do_branch((regs.status & FLAG_OVERFLOW) == 0);
}

static void bvs() {
    _do_branch((regs.status & FLAG_OVERFLOW) == FLAG_OVERFLOW);
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
        if (regs.c >= value) setcarry();
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
    penaltyn = 1;
    regs.pc++;

    interrupt6502(INT_COP);
}

static void cpx() {
    value = getvalue(index_16bit());

    if (index_16bit()) {
        result = regs.x - value;
        if(regs.x >= value) setcarry();
        else clearcarry();
        if (regs.x == value) setzero();
        else clearzero();
    } else {
        result = (uint16_t)regs.xl - value;
        if (regs.xl >= (uint8_t)(value & 0x00FF)) setcarry();
        else clearcarry();
        if (regs.xl == (uint8_t)(value & 0x00FF)) setzero();
        else clearzero();
    }
    signcalc(result, index_16bit());
}

static void cpy() {
    value = getvalue(index_16bit());

    if (index_16bit()) {
        result = regs.y - value;
        if(regs.y >= value) setcarry();
        else clearcarry();
        if (regs.y == value) setzero();
        else clearzero();
    } else {
        result = (uint16_t)regs.yl - value;
        if (regs.yl >= (uint8_t)(value & 0x00FF)) setcarry();
        else clearcarry();
        if (regs.yl == (uint8_t)(value & 0x00FF)) setzero();
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
        regs.x--;
        zerocalc(regs.x, 1);
        signcalc(regs.x, 1);
    } else {
        regs.xl--;
        zerocalc(regs.xl, 0);
        signcalc(regs.xl, 0);
    }
}

static void dey() {
    if (index_16bit()) {
        regs.y--;
        zerocalc(regs.y, 1);
        signcalc(regs.y, 1);
    } else {
        regs.yl--;
        zerocalc(regs.yl, 0);
        signcalc(regs.yl, 0);
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
        regs.x++;
        zerocalc(regs.x, 1);
        signcalc(regs.x, 1);
    } else {
        regs.xl++;
        zerocalc(regs.xl, 0);
        signcalc(regs.xl, 0);
    }
}

static void iny() {
    if (index_16bit()) {
        regs.y++;
        zerocalc(regs.y, 1);
        signcalc(regs.y, 1);
    } else {
        regs.yl++;
        zerocalc(regs.yl, 0);
        signcalc(regs.yl, 0);
    }
}

static void jml() {
    regs.pc = ea;
    regs.k = eal;
}

static void jmp() {
    regs.pc = ea;
}

static void jsr() {
    push16(regs.pc - 1);
    regs.pc = ea;
}

static void jsl() {
    push8(regs.k);
    push16(regs.pc - 1);
    regs.pc = ea;
    regs.k = eal;
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
        regs.x = getvalue(1);
        zerocalc(regs.x, 1);
        signcalc(regs.x, 1);
    } else {
        value = getvalue(0);
        regs.xl = (uint8_t)(value & 0x00FF);
        zerocalc(regs.xl, 0);
        signcalc(regs.xl, 0);
    }
}

static void ldy() {
    penaltyop = 1;
    penaltyx = 1;

    if (index_16bit()) {
        regs.y = getvalue(1);
        zerocalc(regs.y, 1);
        signcalc(regs.y, 1);
    } else {
        regs.yl = (uint8_t)(getvalue(0) & 0x00FF);
        zerocalc(regs.yl, 0);
        signcalc(regs.yl, 0);
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

static void pei() {
    push16(ea);
}

static void per() {
    push16(regs.pc + reladdr);
}

static void pha() {
    if (memory_16bit()) {
        push16(regs.c);
    } else {
        push8(regs.a);
    }
}

static void phb() {
    push8(regs.db);
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
    regs.db = pull8();
    zerocalc(regs.db, 0);
    signcalc(regs.db, 0);
}

static void pld() {
    regs.dp = pull16();
}

static void plp() {
    regs.status = pull8();
    if (regs.e) {
        regs.status |= FLAG_INDEX_WIDTH | FLAG_MEMORY_WIDTH;
    } else if (regs.status & FLAG_INDEX_WIDTH) {
        regs.xh = 0;
        regs.yh = 0;
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
    result = (value >> 1) | ((regs.status & FLAG_CARRY) << (memory_16bit() ? 15 : 7));

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

    if (regs.e) {
        regs.status |= FLAG_INDEX_WIDTH | FLAG_MEMORY_WIDTH;
    } else {
        if (regs.status & FLAG_INDEX_WIDTH) {
            regs.xh = 0;
            regs.yh = 0;
        }
        regs.k = pull8();
    }
}

static void rtl() {
    value = pull16();
    regs.pc = value + 1;
    regs.k = pull8();
}

static void rts() {
    value = pull16();
    regs.pc = value + 1;
}

static void sbc() {
    penaltyop = 1;

    if (regs.status & FLAG_DECIMAL) {
        uint16_t tmp, tmp2;
        uint32_t tmpc;
        if (memory_16bit()) {
            uint16_t tmp3;
            uint32_t tmp4;
            value = getvalue(1);
            tmp = (regs.c & 0x000F) - (value & 0x000F) + (regs.status & FLAG_CARRY) - 1;
            tmp2 = (regs.c & 0x00F0) - (value & 0x00F0);
            tmp3 = (regs.c & 0x0F00) - (value & 0x0F00);
            tmp4 = (regs.c & 0xF000) - (value & 0xF000);

            if (tmp & 0xFFF0) {
                tmp2 -= 0x0010;
                tmp -= 0x0006;
            }

            if (tmp2 & 0xFF00) {
                tmp3 -= 0x0100;
                tmp2 -= 0x0060;
            }

            if (tmp3 & 0xF000) {
                tmp4 -= 0x1000;
                tmp3 -= 0x0600;
            }

            tmpc = tmp4;
            if (tmp4 >= 0x0000A000) {
                tmp4 -= 0x6000;
            }

            result = (tmp & 0x000F) | (tmp2 & 0x00F0) | (tmp3 & 0x0F00) | (tmp4 & 0xF000);
            uint16_t c_result = (tmp & 0x000F) | (tmp2 & 0x00F0) | (tmp3 & 0x0F00) | (tmpc & 0xF000);

            if (c_result <= regs.c) {
                setcarry();
            } else {
                clearcarry();
            }
            uint16_t ovresult = regs.c + (value ^ 0xFFFF) + (regs.status & FLAG_CARRY);
            overflowcalc16(ovresult, regs.c, value ^ 0xFFFF);
        } else {
            value = getvalue(0);
            tmp = ((uint16_t)regs.a & 0x0F) - (value & 0x0F) + (regs.status & FLAG_CARRY) - 1;
            tmp2 = ((uint16_t)regs.a & 0xF0) - (value & 0xF0);

            if (tmp & 0xFFF0) {
                tmp2 -= 0x10;
                tmp -= 0x06;
            }

            tmpc = tmp2;
            if (tmp2 & 0xFF00) {
                tmp2 -= 0x60;
            }

            result = (tmp & 0x0F) | (tmp2 & 0xF0);
            uint16_t c_result = (tmp & 0x0F) | (tmpc & 0xF0);

            if (c_result <= (uint16_t)regs.a) {
                setcarry();
            } else {
                clearcarry();
            }
            uint8_t ovresult = regs.a + (value ^ 0xFF) + (regs.status & FLAG_CARRY);
            overflowcalc8((uint16_t)ovresult, (uint16_t)regs.a, value ^ 0xFF);
        }

        clockticks6502 += (uint32_t)(!regs.is65c816);
    } else {
        if (memory_16bit()) {
            value = getvalue(1) ^ 0xFFFF;
            result = regs.c + value + (regs.status & FLAG_CARRY);
            overflowcalc16(result, regs.c, value);
        } else {
            value = getvalue(0) ^ 0x00FF;
            result = (uint16_t)regs.a + value + (uint16_t)(regs.status & FLAG_CARRY);
            overflowcalc8(result, (uint16_t)regs.a, value);
        }

        carrycalc(result, memory_16bit());
    }

    zerocalc(result, memory_16bit());
    signcalc(result, memory_16bit());

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
    if (regs.status & FLAG_INDEX_WIDTH) {
        regs.xh = 0;
        regs.yh = 0;
    }
}

static void sta() {
    putvalue(acc_for_mode(), memory_16bit());
}

static void stx() {
    putvalue(index_16bit() ? regs.x : regs.xl, index_16bit());
}

static void sty() {
    putvalue(index_16bit() ? regs.y : regs.yl, index_16bit());
}

static void tax() {
    if (index_16bit()) {
        regs.x = regs.c; // 16 bits transferred, no matter the state of m
        zerocalc(regs.x, 1);
        signcalc(regs.x, 1);
    } else {
        regs.xl = (uint8_t)(regs.a & 0x00FF);
        zerocalc(regs.xl, 0);
        signcalc(regs.xl, 0);
    }
}

static void tay() {
    if (index_16bit()) {
        regs.y = regs.c; // 16 bits transferred, no matter the state of m
        zerocalc(regs.y, 1);
        signcalc(regs.y, 1);
    } else {
        regs.yl = (uint8_t)(regs.a & 0x00FF);
        zerocalc(regs.yl, 0);
        signcalc(regs.yl, 0);
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
        regs.x = regs.sp; // 16 bits transferred, no matter the state of m
        zerocalc(regs.x, 1);
        signcalc(regs.x, 1);
    } else {
        regs.xl = (uint8_t)(regs.sp & 0x00FF);
        regs.xh = 0;
        zerocalc(regs.xl, 0);
        signcalc(regs.xl, 0);
    }
}

static void txa() {
    if (memory_16bit()) {
        if (index_16bit()) {
            regs.c = regs.x;
            zerocalc(regs.c, 1);
            signcalc(regs.c, 1);
        } else {
            regs.a = regs.xl;
            regs.b = 0;
            zerocalc(regs.a, 0);
            signcalc(regs.a, 0);
        }
    } else {
        regs.a = regs.xl;
        zerocalc(regs.a, 0);
        signcalc(regs.a, 0);
    }
}

static void txs() {
    if (regs.e) {
        regs.sp = 0x100 | regs.xl;
    } else {
        regs.sp = regs.x;
    }
}

static void txy() {
    if (index_16bit()) {
        regs.y = regs.x;
        zerocalc(regs.y, 1);
        signcalc(regs.y, 1);
    } else {
        regs.yl = regs.xl;
        zerocalc(regs.yl, 0);
        signcalc(regs.yl, 0);
    }
}

static void tya() {
    if (memory_16bit()) {
        if (index_16bit()) {
            regs.c = regs.y;
            zerocalc(regs.c, 1);
            signcalc(regs.c, 1);
        } else {
            regs.a = regs.yl;
            regs.b = 0;
            zerocalc(regs.a, 0);
            signcalc(regs.a, 0);
        }
    } else {
        regs.a = regs.yl;
        zerocalc(regs.a, 0);
        signcalc(regs.a, 0);
    }
}

static void tyx() {
    if (index_16bit()) {
        regs.x = regs.y;
        zerocalc(regs.x, 1);
        signcalc(regs.x, 1);
    } else {
        regs.xl = regs.yl;
        zerocalc(regs.xl, 0);
        signcalc(regs.xl, 0);
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
    if (index_16bit()) {
        write6502(regs.y++, read6502(regs.x++));
    } else {
        write6502(regs.yl++, read6502(regs.xl++));
    }
    if (--regs.c != 0xFFFF) {
        regs.pc -= 3;
    }
}

static void mvp() {
    if (index_16bit()) {
        write6502(regs.y--, read6502(regs.x--));
    } else {
        write6502(regs.yl--, read6502(regs.xl--));
    }
    if (--regs.c != 0xFFFF) {
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
