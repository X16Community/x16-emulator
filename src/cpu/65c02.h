// *******************************************************************************************
// *******************************************************************************************
//
//		File:		65C02.H
//		Date:		3rd September 2019
//		Purpose:	Additional functions for new 65C02 Opcodes.
//		Author:		Paul Robson (paul@robson.org.uk)
//
// *******************************************************************************************
// *******************************************************************************************

// *******************************************************************************************
//
//					Indirect without indexation.  (copied from indy)
//
// *******************************************************************************************


// *******************************************************************************************
//
//						(Absolute,Indexed) address mode for JMP
//
// *******************************************************************************************

static void ainx() { 		// absolute indexed branch
    uint16_t eahelp, eahelp2;
    eahelp = (uint16_t)read6502(regs.pc) | (uint16_t)((uint16_t)read6502(regs.pc+1) << 8);
    eahelp = (eahelp + regs.x) & 0xFFFF;
#if 0
    eahelp2 = (eahelp & 0xFF00) | ((eahelp + 1) & 0x00FF); //replicate 6502 page-boundary wraparound bug
#else
    eahelp2 = eahelp + 1; // the 65c02 doesn't have the bug
#endif
    ea = (uint16_t)read6502(eahelp) | ((uint16_t)read6502(eahelp2) << 8);
    regs.pc += 2;
}

// *******************************************************************************************
//
//								Store zero to memory.
//
// *******************************************************************************************

static void stz() {
    putvalue(0, memory_16bit());
}

// *******************************************************************************************
//
//								Unconditional Branch
//
// *******************************************************************************************

static void bra() {
    oldpc = regs.pc;
    regs.pc += reladdr;
    if (regs.e && (oldpc & 0xFF00) != (regs.pc & 0xFF00)) clockticks6502++; //check if jump crossed a page boundary
}

// *******************************************************************************************
//
//									Push/Pull X and Y
//
// *******************************************************************************************

static void phx() {
    penaltym = 1;

    if (index_16bit()) {
        push16(regs.x);
    } else {
        push8(regs.xl);
    }
}

static void plx() {
    penaltym = 1;

    if (index_16bit()) {
        regs.x = pull16();
        zerocalc(regs.x, 1);
        signcalc(regs.x, 1);
    } else {
        regs.xl = pull8();

        zerocalc(regs.xl, 0);
        signcalc(regs.xl, 0);
    }
}

static void phy() {
    penaltym = 1;

    if (index_16bit()) {
        push16(regs.y);
    } else {
        push8(regs.yl);
    }
}

static void ply() {
    penaltym = 1;

    if (index_16bit()) {
        regs.y = pull16();
        zerocalc(regs.y, 1);
        signcalc(regs.y, 1);
    } else {
        regs.yl = pull8();

        zerocalc(regs.xl, 0);
        signcalc(regs.xl, 0);
    }
}

// *******************************************************************************************
//
//								TRB & TSB - Test and Change bits
//
// *******************************************************************************************

static void tsb() {
    value = getvalue(memory_16bit()); 							// Read memory
    result = acc_for_mode() & value;                // calculate A & memory
    zerocalc(result, memory_16bit()); 								// Set Z flag from this.
    result = value | acc_for_mode(); 				// Write back value read, A bits are set.
    putvalue(result, memory_16bit());
}

static void trb() {
    value = getvalue(memory_16bit()); 							// Read memory
    result = acc_for_mode() & value;  			// calculate A & memory
    zerocalc(result, memory_16bit()); 								// Set Z flag from this.
    result = value & (memory_16bit() ? regs.c ^ 0xFFFF : regs.a ^ 0xFF); 		    	// Write back value read, A bits are clear.
    putvalue(result, memory_16bit());
}

// *******************************************************************************************
//
//                                   Stop (Invoke Debugger)
//
// *******************************************************************************************

static void dbg() {
    stop6502(regs.pc - 1);
}

// *******************************************************************************************
//
//                                     Wait for interrupt
//
// *******************************************************************************************

static void wai() {
	waiting = 1;
}

// *******************************************************************************************
//
//                                     BBR and BBS
//
// *******************************************************************************************
static void bbr(uint16_t bitmask)
{
	if ((getvalue(0) & bitmask) == 0) {
		oldpc = regs.pc;
		regs.pc += reladdr;
		if ((oldpc & 0xFF00) != (regs.pc & 0xFF00)) clockticks6502 += 2; //check if jump crossed a page boundary
		else clockticks6502++;
	}
}

static void bbr0() { bbr(0x01); }
static void bbr1() { bbr(0x02); }
static void bbr2() { bbr(0x04); }
static void bbr3() { bbr(0x08); }
static void bbr4() { bbr(0x10); }
static void bbr5() { bbr(0x20); }
static void bbr6() { bbr(0x40); }
static void bbr7() { bbr(0x80); }

static void bbs(uint16_t bitmask)
{
	if ((getvalue(0) & bitmask) != 0) {
		oldpc = regs.pc;
		regs.pc += reladdr;
		if ((oldpc & 0xFF00) != (regs.pc & 0xFF00)) clockticks6502 += 2; //check if jump crossed a page boundary
		else clockticks6502++;
	}
}

static void bbs0() { bbs(0x01); }
static void bbs1() { bbs(0x02); }
static void bbs2() { bbs(0x04); }
static void bbs3() { bbs(0x08); }
static void bbs4() { bbs(0x10); }
static void bbs5() { bbs(0x20); }
static void bbs6() { bbs(0x40); }
static void bbs7() { bbs(0x80); }

// *******************************************************************************************
//
//                                     SMB and RMB
//
// *******************************************************************************************

static void smb0() { putvalue(getvalue(0) | 0x01, 0); }
static void smb1() { putvalue(getvalue(0) | 0x02, 0); }
static void smb2() { putvalue(getvalue(0) | 0x04, 0); }
static void smb3() { putvalue(getvalue(0) | 0x08, 0); }
static void smb4() { putvalue(getvalue(0) | 0x10, 0); }
static void smb5() { putvalue(getvalue(0) | 0x20, 0); }
static void smb6() { putvalue(getvalue(0) | 0x40, 0); }
static void smb7() { putvalue(getvalue(0) | 0x80, 0); }

static void rmb0() { putvalue(getvalue(0) & ~0x01, 0); }
static void rmb1() { putvalue(getvalue(0) & ~0x02, 0); }
static void rmb2() { putvalue(getvalue(0) & ~0x04, 0); }
static void rmb3() { putvalue(getvalue(0) & ~0x08, 0); }
static void rmb4() { putvalue(getvalue(0) & ~0x10, 0); }
static void rmb5() { putvalue(getvalue(0) & ~0x20, 0); }
static void rmb6() { putvalue(getvalue(0) & ~0x40, 0); }
static void rmb7() { putvalue(getvalue(0) & ~0x80, 0); }
