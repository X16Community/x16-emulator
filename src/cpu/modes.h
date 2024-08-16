/*

						Extracted from original single fake6502.c file

*/

//
//          65C02 changes.
//
//          ind         absolute indirect
//
//                      A 6502 has a bug whereby if you jmp ($12FF) it reads the address from
//                      $12FF and $1200. This has been fixed in the 65C02.
//
static void imp() { //implied
}

static void imp8() { // brk / cop
}

static void acc() { //accumulator
}

static void imm8() { //immediate, 8bit
    ea = regs.pc++;
}

static void immm() { //immediate, 16bit if M = 0
    ea = regs.pc++;

    if (memory_16bit()) {
        regs.pc++;
    }
}

static void immx() { //immediate, 16bit if M = 0
    ea = regs.pc++;

    if (index_16bit()) {
        regs.pc++;
    }
}

static void imm16() {
    ea = regs.pc;
    regs.pc += 2;
}

static void _zp_with_offset(uint16_t offset) {
    uint16_t imm_value = (uint16_t) read6502((uint16_t)regs.pc++);

    if (regs.dp & 0x00FF) {
        penaltyd = 1;
    }

    ea = direct_page_add(imm_value + offset);
}

static void zp() { //zero-page
    _zp_with_offset(0);
}

static void zpx() { //zero-page,X
    _zp_with_offset(regs.x);
}

static void zpy() { //zero-page,Y
    _zp_with_offset(regs.y);
}

static void rel() { //relative for branch ops (8-bit immediate value, sign-extended)
    reladdr = (uint16_t)read6502(regs.pc++);
    if (reladdr & 0x80) reladdr |= 0xFF00;
}

static void rel16() { //relative for PER and BRL (16-bit immediate value)
    reladdr = (uint16_t)read6502(regs.pc) | ((uint16_t)read6502(regs.pc+1) << 8);
    regs.pc += 2;
}

static void abso() { //absolute
    ea = (uint16_t)read6502(regs.pc) | ((uint16_t)read6502(regs.pc+1) << 8);
    regs.pc += 2;
}

static void absx() { //absolute,X
    uint16_t startpage;
    ea = ((uint16_t)read6502(regs.pc) | ((uint16_t)read6502(regs.pc+1) << 8));
    startpage = ea & 0xFF00;
    ea += regs.x;

    if (startpage != (ea & 0xFF00)) { //one cycle penlty for page-crossing on some opcodes
        penaltyaddr = 1;
    }

    regs.pc += 2;
}

static void absy() { //absolute,Y
    uint16_t startpage;
    ea = ((uint16_t)read6502(regs.pc) | ((uint16_t)read6502(regs.pc+1) << 8));
    startpage = ea & 0xFF00;
    ea += regs.y;

    if (startpage != (ea & 0xFF00)) { //one cycle penlty for page-crossing on some opcodes
        penaltyaddr = 1;
    }

    regs.pc += 2;
}

static void ind() { //indirect
    uint16_t eahelp, eahelp2;
    eahelp = (uint16_t)read6502(regs.pc) | (uint16_t)((uint16_t)read6502(regs.pc+1) << 8);
    //
    //      The 6502 page boundary wraparound bug does not occur on a 65C02.
    //
    //eahelp2 = (eahelp & 0xFF00) | ((eahelp + 1) & 0x00FF); //replicate 6502 page-boundary wraparound bug
    eahelp2 = (eahelp+1) & 0xFFFF;
    ea = (uint16_t)read6502(eahelp) | ((uint16_t)read6502(eahelp2) << 8);
    regs.pc += 2;
}

static void ind0() { // (zp)
    uint16_t eahelp;
    eahelp = (uint16_t)read6502(regs.pc++);
    ea = (uint16_t)read6502(direct_page_add(eahelp)) | ((uint16_t)read6502(direct_page_add(eahelp + 1)) << 8);

    if (regs.dp & 0x00FF) {
        penaltyd = 1;
    }
}

static void indx() { // (indirect,X)
    uint16_t eahelp;
    eahelp = (uint16_t)read6502(regs.pc++) + regs.x;
    ea = (uint16_t)read6502(direct_page_add(eahelp)) | ((uint16_t)read6502(direct_page_add(eahelp + 1)) << 8);

    if (regs.dp & 0x00FF) {
        penaltyd = 1;
    }
}

static void indy() { // (indirect),Y
    uint16_t eahelp, startpage;
    eahelp = (uint16_t)read6502(regs.pc++);
    ea = (uint16_t)read6502(direct_page_add(eahelp)) | ((uint16_t)read6502(direct_page_add(eahelp + 1)) << 8);
    startpage = ea & 0xFF00;
    ea += regs.y;

    if (regs.dp & 0x00FF) {
        penaltyd = 1;
    }

    if (startpage != (ea & 0xFF00)) { //one cycle penlty for page-crossing on some opcodes
        penaltyaddr = 1;
    }
}

static void ind0p() { // (zp) used by PEI, which doesn't do wraparound calculations
    uint16_t eahelp;
    eahelp = (uint16_t)read6502(regs.pc++);
    ea = (uint16_t)read6502(regs.dp + eahelp) | ((uint16_t)read6502(regs.dp + eahelp + 1) << 8);

    if (regs.dp & 0x00FF) {
        penaltyd = 1;
    }
}

static void zprel() { // zero-page, relative for branch ops (8-bit immediatel value, sign-extended)
	ea = (uint16_t)read6502(regs.pc);
	reladdr = (uint16_t)read6502(regs.pc+1);
	if (reladdr & 0x80) reladdr |= 0xFF00;

	regs.pc += 2;
}

static void sr() { // absolute,S
    ea = regs.sp + (uint16_t)read6502(regs.pc++);
}

static void sridy() { // (indirect,S),Y
    uint16_t eahelp, startpage;
    eahelp = regs.sp + (uint16_t)read6502(regs.pc++);
    ea = (uint16_t)read6502(eahelp) | ((uint16_t)read6502(eahelp + 1) << 8);
    startpage = ea & 0xFF00;
    ea += (uint16_t)regs.y;

    if (startpage != (ea & 0xFF00)) { //one cycle penlty for page-crossing on some opcodes
        penaltyaddr = 1;
    }
}

static void bmv() { // block move
    uint8_t src = regs.pc++;
    ea = (src << 8) | regs.pc++;
}

static void absl() { // absolute long
    abso();
    eal = read6502(regs.pc++);
}

static void abslx() { // absolute long, X
    absx();
    eal = read6502(regs.pc++);
}

static void aindl() { // [addr]
    ind();
    eal = read6502(regs.pc++);
}

static void _zp_long_with_offset(uint16_t offset) {
    uint16_t eahelp;
    eahelp = (uint16_t)read6502(regs.pc++);

    uint32_t ea32 = (uint16_t)read6502(regs.dp + eahelp) | ((uint16_t)read6502(regs.dp + eahelp + 1) << 8) | ((uint16_t)read6502(regs.dp + eahelp + 2) << 16);
    ea32 += offset;

    ea = (uint16_t) ea32;
    eal = (uint8_t) (ea32 >> 16);

    if (regs.dp & 0x00FF) {
        penaltyd = 1;
    }
}

static void indl0() { // [dp]
    _zp_long_with_offset(0);
}

static void indly() { // [dp],Y
    _zp_long_with_offset(regs.y);
}
