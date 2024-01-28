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

static void ind0() {
    uint16_t eahelp;
    eahelp = (uint16_t)read6502(regs.pc++);
    ea = (uint16_t)read6502(direct_page_add(eahelp)) | ((uint16_t)read6502(direct_page_add(eahelp + 1)) << 8);
}


// *******************************************************************************************
//
//						(Absolute,Indexed) address mode for JMP
//
// *******************************************************************************************

static void ainx() { 		// absolute indexed branch
    uint16_t eahelp, eahelp2;
    eahelp = (uint16_t)read6502(regs.pc) | (uint16_t)((uint16_t)read6502(regs.pc+1) << 8);
    eahelp = (eahelp + regs.xw) & 0xFFFF;
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
        push16(regs.xw);
    } else {
        push8(regs.x);
    }
}

static void plx() {
    penaltym = 1;

    if (index_16bit()) {
        regs.xw = pull16();
        zerocalc(regs.xw, 1);
        signcalc(regs.xw, 1);
    } else {
        regs.x = pull8();
    }

    zerocalc(regs.x, 0);
    signcalc(regs.x, 0);
}

static void phy() {
    penaltym = 1;

    if (index_16bit()) {
        push16(regs.yw);
    } else {
        push8(regs.y);
    }
}

static void ply() {
    penaltym = 1;

    if (index_16bit()) {
        regs.yw = pull16();
        zerocalc(regs.yw, 1);
        signcalc(regs.yw, 1);
    } else {
        regs.y = pull8();

        zerocalc(regs.x, 0);
        signcalc(regs.x, 0);
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

static void stp() {
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
