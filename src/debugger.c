
// *******************************************************************************************
// *******************************************************************************************
//
//		File:		debugger.c
//		Date:		5th September 2019
//		Purpose:	Debugger code
//		Author:		Paul Robson (paul@robson.org.uk)
//
// *******************************************************************************************
// *******************************************************************************************

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>
#include <SDL.h>
#include "glue.h"
#include "timing.h"
#include "disasm.h"
#include "memory.h"
#include "video.h"
#include "cpu/fake6502.h"
#include "debugger.h"
#include "rendertext.h"

static void DEBUGHandleKeyEvent(SDL_Keycode key,int isShift);

static void DEBUGNumber(int x,int y,int n,int w, SDL_Color colour);
static void DEBUGNumberDec(int x, int y, int n, int w, SDL_Color colour);
static void DEBUGAddress(int x, int y, int bank, int addr, SDL_Color colour);
static void DEBUGVAddress(int x, int y, int addr, SDL_Color colour);

static void DEBUGRenderData(int y,int data);
static int DEBUGRenderZeroPageRegisters(int y);
static void DEBUGRenderVERAState(int y);
static int DEBUGRenderRegisters(void);
static void DEBUGRenderVRAM(int y, int data);
static void DEBUGRenderCode(int lines,int initialPC);
static void DEBUGRenderStack(int bytesCount);
static void DEBUGRenderCmdLine(int x, int width, int height);
static bool DEBUGBuildCmdLine(SDL_Keycode key);
static void DEBUGExecCmd();

// *******************************************************************************************
//
//		This is the minimum-interference flag. It's designed so that when
//		its non-zero DEBUGRenderDisplay() is called.
//
//			if (showDebugOnRender != 0) {
//				DEBUGRenderDisplay(SCREEN_WIDTH,SCREEN_HEIGHT,renderer);
//				SDL_RenderPresent(renderer);
//				return true;
//			}
//
//		before the SDL_RenderPresent call in video_update() in video.c
//
//		This controls what is happening. It is at the top of the main loop in main.c
//
//			if (isDebuggerEnabled != 0) {
//				int dbgCmd = DEBUGGetCurrentStatus();
//				if (dbgCmd > 0) continue;
//				if (dbgCmd < 0) break;
//			}
//
//		Both video.c and main.c require debugger.h to be included.
//
//		isDebuggerEnabled should be a flag set as a command line switch - without it
//		it will run unchanged. It should not be necessary to test the render code
//		because showDebugOnRender is statically initialised to zero and will only
//		change if DEBUGGetCurrentStatus() is called.
//
// *******************************************************************************************

//
//				0-9A-F sets the program address, with shift sets the data address.
//
#define DBGKEY_HOME     SDLK_F1                         // F1 is "Goto PC"
#define DBGKEY_RESET    SDLK_F2                         // F2 resets the 6502
#define DBGKEY_RUN      SDLK_F5                         // F5 is run.
#define DBGKEY_SETBRK   SDLK_F9                         // F9 sets breakpoint
#define DBGKEY_STEP     SDLK_F11                        // F11 is step into.
#define DBGKEY_STEPOVER SDLK_F10                        // F10 is step over.
#define DBGKEY_BANK_NEXT	SDLK_KP_PLUS
#define DBGKEY_BANK_PREV	SDLK_KP_MINUS

#define DBGSCANKEY_BRK  SDL_SCANCODE_F12                // F12 is break into running code.
#define DBGSCANKEY_SHOW SDL_SCANCODE_TAB                // Show screen key.
                                                        // *** MUST BE SCAN CODES ***

#define DBGMAX_ZERO_PAGE_REGISTERS 16

#define DDUMP_RAM	0
#define DDUMP_VERA	1

enum DBG_CMD { CMD_DUMP_MEM='m', CMD_DUMP_VERA='v', CMD_DISASM='d', CMD_SET_BANK='b', CMD_SET_REGISTER='r', CMD_FILL_MEMORY='f' };

// RGB colours
const SDL_Color col_bkgnd= {0, 0, 0, 255};
const SDL_Color col_label= {0, 255, 0, 255};
const SDL_Color col_data= {0, 255, 255, 255};
const SDL_Color col_highlight= {255, 255, 0, 255};
const SDL_Color col_cmdLine= {255, 255, 255, 255};

const SDL_Color col_directpage= {192, 224, 255, 255};

const SDL_Color col_vram_tilemap = {0, 255, 255, 255};
const SDL_Color col_vram_tiledata = {0, 255, 0, 255};
const SDL_Color col_vram_special  = {255, 92, 92, 255};
const SDL_Color col_vram_other  = {128, 128, 128, 255};

int showDebugOnRender = 0;                               // Used to trigger rendering in video.c
int showFullDisplay = 0;                                 // If non-zero show the whole thing.
int currentPC = -1;                                      // Current PC value.
int currentData = 0;                                     // Current data display address.
int currentPCBank = -1;
int currentBank = -1;
int currentMode = DMODE_RUN;                             // Start running.
uint32_t debugCPUClocks = 0;

int dumpmode          = DDUMP_RAM;

struct breakpoint breakPoint = { -1, -1 };               // User Break
struct breakpoint stepBreakPoint = { -1, -1 };           // Single step break.

char cmdLine[64]= "";                                    // command line buffer
int currentPosInLine= 0;                                 // cursor position in the buffer (NOT USED _YET_)
int currentLineLen= 0;                                   // command line buffer length

int    oldRegisters[DBGMAX_ZERO_PAGE_REGISTERS];      // Old ZP Register values, for change detection
char * oldRegChange[DBGMAX_ZERO_PAGE_REGISTERS];      // Change notification flags for output
int    oldRegisterTicks = 0;                          // Last PC when change notification was run

//
//		This flag controls
//

SDL_Renderer *dbgRenderer;                            // Renderer passed in.

static inline int getCurrentBank(int pc) {
	int bank = -1;
	if (pc >= 0xA000) {
		bank = pc < 0xC000 ? memory_get_ram_bank() : memory_get_rom_bank();
	}
	return bank;
}

// *******************************************************************************************
//
//      	This determines if we have hit a breakpoint, both in pc and bank
//
// *******************************************************************************************

static inline bool hitBreakpoint(int pc, struct breakpoint bp) {
	if ((pc == bp.pc) && getCurrentBank(pc) == bp.bank) {
		return true;
	}
	return false;
}

// *******************************************************************************************
//
//			This is used to determine who is in control. If it returns zero then
//			everything runs normally till the next call.
//			If it returns +ve, then it will update the video, and loop round.
//			If it returns -ve, then exit.
//
// *******************************************************************************************

int  DEBUGGetCurrentStatus(void) {

	SDL_Event event;
	if (currentPC < 0) currentPC = regs.pc;                                      // Initialise current PC displayed.

	if (currentMode == DMODE_STEP) {                                        // Single step before
		if (currentPC != regs.pc || currentPCBank != getCurrentBank(regs.pc)) {   // Ensure that the PC moved
			currentPC = regs.pc;                                         // Update current PC
			currentPCBank = getCurrentBank(regs.pc);                     // Update the bank if we are in upper memory.
			currentMode = DMODE_STOP;                               // So now stop, as we've done it.
		}
	}

	if ((currentMode != DMODE_STOP) && (hitBreakpoint(regs.pc, breakPoint) || hitBreakpoint(regs.pc, stepBreakPoint))) {       // Hit a breakpoint.
		currentPC = regs.pc;                                                         // Update current PC
		currentPCBank = getCurrentBank(regs.pc);                                     // Update the bank if we are in upper memory.
		currentMode = DMODE_STOP;                                               // So now stop, as we've done it.
		stepBreakPoint.pc = -1;                                                 // Clear step breakpoint.
		stepBreakPoint.bank = -1;
	}

	if (SDL_GetKeyboardState(NULL)[DBGSCANKEY_BRK]) {                       // Stop on break pressed.
		currentMode = DMODE_STOP;
		currentPC = regs.pc;                                                 // Set the PC to what it is.
		currentPCBank = getCurrentBank(regs.pc);                             // Update the bank if we are in upper memory.
	}

	if (currentPCBank<0 && currentPC >= 0xA000) {
		currentPCBank = currentPC < 0xC000 ? memory_get_ram_bank() : memory_get_rom_bank();
	}

	if (currentMode != DMODE_RUN) {                                         // Not running, we own the keyboard.
		showFullDisplay =                                               // Check showing screen.
					SDL_GetKeyboardState(NULL)[DBGSCANKEY_SHOW];
		while (SDL_PollEvent(&event)) {                                 // We now poll events here.
			switch(event.type) {
				case SDL_QUIT:                                  // Time for exit
					return -1;

				case SDL_KEYDOWN:                               // Handle key presses.
					DEBUGHandleKeyEvent(event.key.keysym.sym,
										SDL_GetModState() & (KMOD_LSHIFT|KMOD_RSHIFT));
					break;

			}
		}
	}

	showDebugOnRender = (currentMode != DMODE_RUN);                         // Do we draw it - only in RUN mode.
	if (currentMode == DMODE_STOP) {                                        // We're in charge.
		video_update();
		SDL_Delay(10);
		return 1;
	}

	return 0;                                                               // Run wild, run free.
}

// *******************************************************************************************
//
//								Setup fonts and co
//
// *******************************************************************************************
void DEBUGInitUI(SDL_Renderer *pRenderer) {
		DEBUGInitChars(pRenderer);
		dbgRenderer = pRenderer;				// Save renderer.
}

// *******************************************************************************************
//
//								Setup fonts and co
//
// *******************************************************************************************
void DEBUGFreeUI() {
}

// *******************************************************************************************
//
//								Set a new breakpoint address. -1 to disable.
//
// *******************************************************************************************

void DEBUGSetBreakPoint(struct breakpoint newBreakPoint) {
	breakPoint = newBreakPoint;
}

// *******************************************************************************************
//
//								Break into debugger from code.
//
// *******************************************************************************************

void DEBUGBreakToDebugger(void) {
	currentMode = DMODE_STOP;
	currentPC = regs.pc;
	currentPCBank = getCurrentBank(regs.pc);
}

// *******************************************************************************************
//
//									Handle keyboard state.
//
// *******************************************************************************************

static void DEBUGHandleKeyEvent(SDL_Keycode key,int isShift) {
	int opcode;

	switch(key) {

		case DBGKEY_STEP:								// Single step (F11 by default)
			currentMode = DMODE_STEP; 						// Runs once, then switches back.
			debugCPUClocks = clockticks6502;
			break;

		case DBGKEY_STEPOVER:								// Step over (F10 by default)
			opcode = debug_read6502(regs.pc, currentPCBank);			// What opcode is it ?
			if (opcode == 0x20 || opcode == 0xFC || opcode == 0x22) { 		// Is it JSR or JSL ?
				stepBreakPoint.pc = regs.pc + 3 + (opcode == 0x22);			// Then break 3 / 4 on.
				stepBreakPoint.bank = getCurrentBank(regs.pc);
				currentMode = DMODE_RUN;					// And run.
				debugCPUClocks = clockticks6502;
				timing_init();
			} else {
				currentMode = DMODE_STEP;					// Otherwise single step.
				debugCPUClocks = clockticks6502;
			}
			break;

		case DBGKEY_RUN:								// F5 Runs until Break.
			currentMode = DMODE_RUN;
			debugCPUClocks = clockticks6502;
			timing_init();
			break;

		case DBGKEY_SETBRK:								// F9 Set breakpoint to displayed.
			breakPoint.pc = currentPC;
			breakPoint.bank = currentPCBank;
			break;

		case DBGKEY_HOME:								// F1 sets the display PC to the actual one.
			currentPC = regs.pc;
			currentPCBank= getCurrentBank(regs.pc);
			break;

		case DBGKEY_RESET:								// F2 reset the 6502
			reset6502(regs.is65c816);
			currentPC = regs.pc;
			currentPCBank= -1;
			break;

		case DBGKEY_BANK_NEXT:
			currentBank += 1;
			break;

		case DBGKEY_BANK_PREV:
			currentBank -= 1;
			break;

		case SDLK_PAGEDOWN:
			if (isShift) {
				currentPC = (currentPC + 0x10) & 0xffff;
			} else {
				if (dumpmode == DDUMP_RAM) {
					currentData = (currentData + 0x100) & 0xFFFF;
				} else {
					currentData = (currentData + 0x200) & 0x1FFFF;
				}
			}
			break;

		case SDLK_PAGEUP:
			if (isShift) {
				currentPC = (currentPC - 0x10) & 0xffff;
			} else {
				if (dumpmode == DDUMP_RAM) {
					currentData = (currentData - 0x100) & 0xFFFF;
				} else {
					currentData = (currentData - 0x200) & 0x1FFFF;
				}
			}
			break;

		case SDLK_DOWN:
			if (isShift) {
				currentPC = (currentPC + 1) & 0xffff;
			} else {
				if (dumpmode == DDUMP_RAM) {
					currentData = (currentData + 0x08) & 0xFFFF;
				} else {
					currentData = (currentData + 0x10) & 0x1FFFF;
				}
			}
			break;

		case SDLK_UP:
			if (isShift) {
				currentPC = (currentPC - 1) & 0xffff;
			} else {
				if (dumpmode == DDUMP_RAM) {
					currentData = (currentData - 0x08) & 0xFFFF;
				} else {
					currentData = (currentData - 0x10) & 0x1FFFF;
				}
			}
			break;

		default:
			if(DEBUGBuildCmdLine(key)) {
				// printf("cmd line: %s\n", cmdLine);
				DEBUGExecCmd();
			}
			break;
	}

}

char kNUM_KEYPAD_CHARS[10] = {'1','2','3','4','5','6','7','8','9','0'};

static bool DEBUGBuildCmdLine(SDL_Keycode key) {
	// right now, let's have a rudimentary input: only backspace to delete last char
	// later, I want a real input line with delete, backspace, left and right cursor
	// devs like their comfort ;)
	if(currentLineLen <= sizeof(cmdLine)) {
		if(
			(key >= SDLK_SPACE && key <= SDLK_AT)
			||
			(key >= SDLK_LEFTBRACKET && key <= SDLK_z)
			||
			(key >= SDLK_KP_1 && key <= SDLK_KP_0)
			) {
			cmdLine[currentPosInLine++]= key>=SDLK_KP_1 ? kNUM_KEYPAD_CHARS[key-SDLK_KP_1] : key;
			if(currentPosInLine > currentLineLen) {
				currentLineLen++;
			}
		} else if(key == SDLK_BACKSPACE) {
			currentPosInLine--;
			if(currentPosInLine<0) {
				currentPosInLine= 0;
			}
			currentLineLen--;
			if(currentLineLen<0) {
				currentLineLen= 0;
			}
		}
		cmdLine[currentLineLen]= 0;
	}
	return (key == SDLK_RETURN) || (key == SDLK_KP_ENTER);
}

static void DEBUGExecCmd() {
	int number, addr, size, incr;
	char reg[10];
	char cmd;
	char *line= ltrim(cmdLine);

	cmd= *line;
	if(*line) {
		line++;
	}
	// printf("cmd:%c line: '%s'\n", cmd, line);

	switch (cmd) {
		case CMD_DUMP_MEM:
			sscanf(line, "%x", &number);
			addr= number & 0xFFFF;
			// Banked Memory, RAM then ROM
			if(addr >= 0xA000) {
				currentBank= (number & 0xFF0000) >> 16;
			}
			currentData= addr;
			dumpmode    = DDUMP_RAM;
			break;

		case CMD_DUMP_VERA:
			sscanf(line, "%x", &number);
			addr = number & 0x1FFFF;
			currentData = addr;
			dumpmode    = DDUMP_VERA;
			break;

		case CMD_FILL_MEMORY:
			size = 1;
			sscanf(line, "%x %x %d %d", &addr, &number, &size, &incr);

			if (dumpmode == DDUMP_RAM) {
				addr &= 0xFFFF;
				do {
					if (addr >= 0xC000) {
						// Nop.
					} else if (addr >= 0xA000) {
						RAM[0xa000 + (currentBank << 13) + addr - 0xa000] = number;
					} else {
						RAM[addr] = number;
					}
					if (incr) {
						addr += incr;
					} else {
						++addr;
					}
					addr &= 0xFFFF;
					--size;
				} while (size > 0);
			} else {
				addr &= 0x1FFFF;
				do {
					video_space_write(addr, number);
					if (incr) {
						addr += incr;
					} else {
						++addr;
					}
					addr &= 0x1FFFF;
					--size;
				} while (size > 0);
			}
			break;

		case CMD_DISASM:
			sscanf(line, "%x", &number);
			addr= number & 0xFFFF;
			// Banked Memory, RAM then ROM
			if(addr >= 0xA000) {
				currentPCBank= (number & 0xFF0000) >> 16;
			}
			else {
				currentPCBank= -1;
			}
			currentPC= addr;
			break;

		case CMD_SET_BANK:
			sscanf(line, "%s %d", reg, &number);

			if(!strcmp(reg, "rom")) {
				memory_set_rom_bank(number & 0x00FF);
			}
			if(!strcmp(reg, "ram")) {
				memory_set_ram_bank(number & 0x00FF);
			}
			break;

		case CMD_SET_REGISTER:
			sscanf(line, "%s %x", reg, &number);

			if(!strcmp(reg, "pc")) {
				regs.pc= number & 0xFFFF;
				waiting = 0;
			}
			if(!strcmp(reg, "a")) {
				regs.a= number & 0x00FF;
			}
			if(!strcmp(reg, "b")) {
				regs.b= number & 0x00FF;
			}
			if(!strcmp(reg, "c")) {
				regs.c= number & 0xFFFF;
			}
			if(!strcmp(reg, "d")) {
				regs.dp= number & 0xFFFF;
			}
			if(!strcmp(reg, "k")) {
				regs.k= number & 0x00FF;
			}
			if(!strcmp(reg, "dbr")) {
				regs.db= number & 0x00FF;
			}
			if(!strcmp(reg, "x")) {
				if (regs.e) {
					regs.xl= number & 0x00FF;
				} else {
					regs.x= number & 0xFFFF;
				}
			}
			if(!strcmp(reg, "y")) {
				if (regs.e) {
					regs.yl= number & 0x00FF;
				} else {
					regs.y= number & 0xFFFF;
				}
			}
			if(!strcmp(reg, "sp")) {
				if (regs.e) {
					regs.sp= 0x100 | (number & 0x00FF);
				} else {
					regs.sp= number & 0xFFFF;
				}
			}
			break;

		default:
			break;
	}

	currentPosInLine= currentLineLen= *cmdLine= 0;
}

// *******************************************************************************************
//
//							Render the emulator debugger display.
//
// *******************************************************************************************

void DEBUGRenderDisplay(int width, int height) {
	if (showFullDisplay) return;								// Not rendering debug.

	SDL_Rect rc;
	rc.w = DBG_WIDTH * 6 * CHAR_SCALE;							// Erase background, set up rect
	rc.h = height;
	xPos = width-rc.w;yPos = 0; 								// Position screen
	rc.x = xPos;rc.y = yPos; 									// Set rectangle and black out.
	SDL_SetRenderDrawColor(dbgRenderer,0,0,0,SDL_ALPHA_OPAQUE);
	SDL_RenderFillRect(dbgRenderer,&rc);

	int register_lines = DEBUGRenderRegisters();							// Draw register name and values.
	DEBUGRenderCode(register_lines, currentPC);							// Render 6502 disassembly.
	if (dumpmode == DDUMP_RAM) {
		DEBUGRenderData(register_lines + 1, currentData);
		int zp_lines = DEBUGRenderZeroPageRegisters(register_lines + 1);
		DEBUGRenderVERAState(zp_lines + 1);
	} else {
		DEBUGRenderVRAM(register_lines + 1, currentData);
	}
	DEBUGRenderStack(register_lines);

	DEBUGRenderCmdLine(xPos, rc.w, height);
}

// *******************************************************************************************
//
//									 Render command Line
//
// *******************************************************************************************

static void DEBUGRenderCmdLine(int x, int width, int height) {
	char buffer[sizeof(cmdLine)+1];

	SDL_SetRenderDrawColor(dbgRenderer, 255, 255, 255, SDL_ALPHA_OPAQUE);
	SDL_RenderDrawLine(dbgRenderer, x, height-12, x+width, height-12);

	sprintf(buffer, ">%s", cmdLine);
	DEBUGString(dbgRenderer, 0, DBG_HEIGHT-1, buffer, col_cmdLine);
}

// *******************************************************************************************
//
//									 Render Zero Page Registers
//
// *******************************************************************************************

static int DEBUGRenderZeroPageRegisters(int y) {
#define LAST_R 15
	unsigned char reg = 0;
	int y_start = y;
	char lbl[6];
	while (reg < DBGMAX_ZERO_PAGE_REGISTERS) {
		if (((y-y_start) % 5) != 0) {           // Break registers into groups of 5, easier to locate
			if (reg <= LAST_R)
				sprintf(lbl, "R%d", reg);
			else
				sprintf(lbl, "x%d", reg);

			DEBUGString(dbgRenderer, DBG_ZP_REG, y, lbl, col_label);

			int reg_addr = 2 + reg * 2;
			int n = debug_read6502(direct_page_add(reg_addr+1), USE_CURRENT_BANK)*256+debug_read6502(direct_page_add(reg_addr), USE_CURRENT_BANK);

			DEBUGNumber(DBG_ZP_REG+5, y, n, 4, col_data);

			if (oldRegChange[reg] != NULL)
				DEBUGString(dbgRenderer, DBG_ZP_REG+9, y, oldRegChange[reg], col_data);

			if (oldRegisterTicks != clockticks6502) {   // change detection only when the emulated CPU changes
				oldRegChange[reg] = n != oldRegisters[reg] ? "*" : " ";
				oldRegisters[reg]=n;
			}
			reg++;
		}
		y++;
	}

	if (oldRegisterTicks != clockticks6502) {
		oldRegisterTicks = clockticks6502;
	}

	return y;
}

// *******************************************************************************************
//
//									 Render Data Area
//
// *******************************************************************************************

static void DEBUGRenderData(int y,int data) {
	while (y < DBG_HEIGHT-2) {									// To bottom of screen
		DEBUGAddress(DBG_MEMX, y, (uint8_t)currentBank, data & 0xFFFF, col_label);	// Show label.

		for (int i = 0;i < 8;i++) {
			bool isDP = ((data+i - regs.dp) & 0xffff) < 256;
			int byte = debug_read6502((data+i) & 0xFFFF, currentBank);
			DEBUGNumber(DBG_MEMX+8+i*3,y,byte,2, isDP ? col_directpage : col_data);
			DEBUGWrite(dbgRenderer, DBG_MEMX+33+i,y,byte, isDP ? col_directpage : col_data);
		}
		y++;
		data += 8;
	}
}

static void DEBUGRenderVRAM(int y, int data) {
	while (y < DBG_HEIGHT - 2) {                                                   // To bottom of screen
		DEBUGVAddress(DBG_MEMX, y, data & 0x1FFFF, col_label); // Show label.

		for (int i = 0; i < 16; i++) {
			int addr = (data + i) & 0x1FFFF;
			int byte = video_space_read(addr);

			if (video_is_tilemap_address(addr)) {
				DEBUGNumber(DBG_MEMX + 6 + i * 3, y, byte, 2, col_vram_tilemap);
			} else if (video_is_tiledata_address(addr)) {
				DEBUGNumber(DBG_MEMX + 6 + i * 3, y, byte, 2, col_vram_tiledata);
			} else if (video_is_special_address(addr)) {
				DEBUGNumber(DBG_MEMX + 6 + i * 3, y, byte, 2, col_vram_special);
			} else {
				DEBUGNumber(DBG_MEMX + 6 + i * 3, y, byte, 2, col_vram_other);
			}
		}
		y++;
		data += 16;
	}
}

// *******************************************************************************************
//
//									 Render Disassembly
//
// *******************************************************************************************

static void DEBUGRenderCode(int lines, int initialPC) {
	char buffer[32];
	uint8_t implied_status = regs.status;
	uint8_t implied_e = regs.e;
	uint8_t opcode, operand, carry;

	for (int y = 0; y < lines; y++) { 							// Each line

		DEBUGAddress(DBG_ASMX, y, currentPCBank, initialPC, col_label);
		int32_t eff_addr;

		// Attempt to display the disassembly correctly more often
		// if the code logic is reasonably straightforward with respect
		// to status flags changing in the immediate instruction list

		// This doesn't predict status flags changes except by the few
		// opcodes below. The PLP instruction, for instance, could easily
		// render disassembly following it invalid, but this would have
		// still been true without the added logic, anyway.

		if (regs.is65c816) {
			opcode = debug_read6502(initialPC, currentPCBank);
			switch (opcode) {
				case 0x81: // CLC
					implied_status &= ~FLAG_CARRY;
					;;
				case 0x83: // SEC
					implied_status |= FLAG_CARRY;
					;;
				case 0xC2: // REP
					operand = debug_read6502((initialPC+1) & 0xffff, currentPCBank);
					implied_status = ~operand & implied_status;
					;;
				case 0xE2: // SEP
					operand = debug_read6502((initialPC+1) & 0xffff, currentPCBank);
					implied_status = operand | implied_status;
					;;
				case 0xFB: // XCE
					carry = implied_status & FLAG_CARRY;
					implied_status = (implied_status & ~FLAG_CARRY) | (implied_e ? FLAG_CARRY : 0);
					implied_e = carry != 0;
					;;
				default:
					;;
			}
			if (implied_e) implied_status |= FLAG_INDEX_WIDTH | FLAG_MEMORY_WIDTH;

		}
		int size = disasm(initialPC, RAM, buffer, sizeof(buffer), currentPCBank, implied_status, &eff_addr);	// Disassemble code
		// Output assembly highlighting PC
		DEBUGString(dbgRenderer, DBG_ASMX+8, y, buffer, initialPC == regs.pc ? col_highlight : col_data);
		// Populate effective address
		if (initialPC == regs.pc) {
			if (eff_addr < 0) {
				DEBUGString(dbgRenderer, DBG_DATX, lines-1, "----", col_data);
			} else {
				DEBUGNumber(DBG_DATX, lines-1, eff_addr, 4, col_data);
			}
		}
		initialPC = (initialPC + size) & 0xffff;										// Forward to next
	}
}

// *******************************************************************************************
//
//									Render Register Display
//
// *******************************************************************************************

static char *labels_c816[] = { "NVMXDIZCE","","","A","B","C","X","Y","K","DB","","PC","DP","SP","BKA","BKO","","BRK","EFF", NULL };
static char *labels_c02[] = { "NV-BDIZC","","","A","X","Y","","PC","SP","BKA","BKO","","BRK","EFF", NULL };

static void DEBUGNumberHighByteCondition(int x, int y, int n, bool condition, SDL_Color ifTrue, SDL_Color ifFalse) {
	if (condition) {
		DEBUGNumber(x, y, n >> 8, 2, ifTrue);
		DEBUGNumber(x + 2, y, n & 0xFF, 2, ifFalse);
	} else {
		DEBUGNumber(x, y, n, 4, ifFalse);
	}
}

static int DEBUGRenderRegisters(void) {
	int n = 0,yc = 0;
	if (regs.is65c816) {
		while (labels_c816[n] != NULL) {								// Labels
			DEBUGString(dbgRenderer, DBG_LBLX,n,labels_c816[n], col_label);n++;
		}
		yc++;
		DEBUGNumber(DBG_LBLX, yc, (regs.status >> 7) & 1, 1, col_data);
		DEBUGNumber(DBG_LBLX+1, yc, (regs.status >> 6) & 1, 1, col_data);
		DEBUGNumber(DBG_LBLX+2, yc, (regs.status >> 5) & 1, 1, regs.e ? col_vram_other : col_data);
		DEBUGNumber(DBG_LBLX+3, yc, (regs.status >> 4) & 1, 1, regs.e ? col_vram_other : col_data);
		DEBUGNumber(DBG_LBLX+4, yc, (regs.status >> 3) & 1, 1, col_data);
		DEBUGNumber(DBG_LBLX+5, yc, (regs.status >> 2) & 1, 1, col_data);
		DEBUGNumber(DBG_LBLX+6, yc, (regs.status >> 1) & 1, 1, col_data);
		DEBUGNumber(DBG_LBLX+7, yc, (regs.status >> 0) & 1, 1, col_data);
		DEBUGNumber(DBG_LBLX+8, yc, regs.e, 1, col_data);
		yc+= 2;

		DEBUGNumber(DBG_DATX, yc++, regs.a, 2, col_data);
		DEBUGNumber(DBG_DATX, yc++, regs.b, 2, col_data);
		DEBUGNumber(DBG_DATX, yc++, regs.c, 4, col_data);
		DEBUGNumberHighByteCondition(DBG_DATX, yc++, regs.x, (regs.status >> 4) & 1, col_vram_other, col_data);
		DEBUGNumberHighByteCondition(DBG_DATX, yc++, regs.y, (regs.status >> 4) & 1, col_vram_other, col_data);
		DEBUGNumber(DBG_DATX, yc++, regs.k, 2, col_data);
		DEBUGNumber(DBG_DATX, yc++, regs.db, 2, col_data);
		yc++;

		DEBUGNumber(DBG_DATX, yc++, regs.pc, 4, col_data);
		DEBUGNumber(DBG_DATX, yc++, regs.dp, 4, col_data);
		DEBUGNumberHighByteCondition(DBG_DATX, yc++, regs.sp, regs.e, col_vram_other, col_data);
		DEBUGNumber(DBG_DATX, yc++, memory_get_ram_bank(), 2, col_data);
		DEBUGNumber(DBG_DATX, yc++, memory_get_rom_bank(), 2, col_data);
		yc++;
	} else {
		while (labels_c02[n] != NULL) {									// Labels
			DEBUGString(dbgRenderer, DBG_LBLX,n,labels_c02[n], col_label);n++;
		}
		yc++;
		DEBUGNumber(DBG_LBLX, yc, (regs.status >> 7) & 1, 1, col_data);
		DEBUGNumber(DBG_LBLX+1, yc, (regs.status >> 6) & 1, 1, col_data);
		DEBUGNumber(DBG_LBLX+3, yc, (regs.status >> 4) & 1, 1, col_data);
		DEBUGNumber(DBG_LBLX+4, yc, (regs.status >> 3) & 1, 1, col_data);
		DEBUGNumber(DBG_LBLX+5, yc, (regs.status >> 2) & 1, 1, col_data);
		DEBUGNumber(DBG_LBLX+6, yc, (regs.status >> 1) & 1, 1, col_data);
		DEBUGNumber(DBG_LBLX+7, yc, (regs.status >> 0) & 1, 1, col_data);
		yc+= 2;

		DEBUGNumber(DBG_DATX, yc++, regs.a, 2, col_data);
		DEBUGNumber(DBG_DATX, yc++, regs.xl, 2, col_data);
		DEBUGNumber(DBG_DATX, yc++, regs.yl, 2, col_data);
		yc++;

		DEBUGNumber(DBG_DATX, yc++, regs.pc, 4, col_data);
		DEBUGNumber(DBG_DATX, yc++, regs.sp|0x100, 4, col_data);
		DEBUGNumber(DBG_DATX, yc++, memory_get_ram_bank(), 2, col_data);
		DEBUGNumber(DBG_DATX, yc++, memory_get_rom_bank(), 2, col_data);
		yc++;

	}

	if (breakPoint.pc < 0) {
		DEBUGString(dbgRenderer, DBG_DATX, yc++, "----", col_data);
	} else if (breakPoint.bank < 0) {
		DEBUGNumber(DBG_DATX, yc++, (uint16_t)breakPoint.pc, 4, col_data);
	} else {
		DEBUGNumber(DBG_DATX, yc++, (breakPoint.bank << 16) | breakPoint.pc, 6, col_data);
	}
	yc++;

	return n; 													// Number of code display lines
}


static char *vera_labels[] = { "ADDR0", "ADDR1", "DATA0","DATA1", "CTRL", "VIDEO", "HSCLE", "VSCLE", "FXCTL", "FXMUL", "CACHE", "ACCUM", "", "CLOCKS ELAPSED", NULL };

static void DEBUGRenderVERAState(int y) {
	int n=0;
	int yc=y;
	while (vera_labels[n] != NULL) { // Labels
		DEBUGString(dbgRenderer, DBG_VERA_REGX, yc, vera_labels[n], col_label);n++;yc++;
	}

	yc=y;

	DEBUGNumber(DBG_VERA_REGX+6, yc++, video_get_address(0), 5, col_data);
	DEBUGNumber(DBG_VERA_REGX+6, yc++, video_get_address(1), 5, col_data);
	DEBUGNumber(DBG_VERA_REGX+6, yc++, video_read(3, true), 2, col_data);
	DEBUGNumber(DBG_VERA_REGX+6, yc++, video_read(4, true), 2, col_data);
	DEBUGNumber(DBG_VERA_REGX+6, yc++, video_read(5, true), 2, col_data);
	DEBUGNumber(DBG_VERA_REGX+6, yc++, video_get_dc_value(0), 2, col_data);
	DEBUGNumber(DBG_VERA_REGX+6, yc++, video_get_dc_value(1), 2, col_data);
	DEBUGNumber(DBG_VERA_REGX+6, yc++, video_get_dc_value(2), 2, col_data);
	DEBUGNumber(DBG_VERA_REGX+6, yc++, video_get_dc_value(8), 2, col_data);
	DEBUGNumber(DBG_VERA_REGX+6, yc++, video_get_dc_value(11), 2, col_data);
	DEBUGNumber(DBG_VERA_REGX+6, yc, video_get_dc_value(24), 2, col_data);
	DEBUGNumber(DBG_VERA_REGX+8, yc, video_get_dc_value(25), 2, col_data);
	DEBUGNumber(DBG_VERA_REGX+10, yc, video_get_dc_value(26), 2, col_data);
	DEBUGNumber(DBG_VERA_REGX+12, yc++, video_get_dc_value(27), 2, col_data);
	DEBUGNumber(DBG_VERA_REGX+6, yc++, video_get_fx_accum(), 8, col_data);

	yc+=2;
	DEBUGNumberDec(DBG_VERA_REGX, yc++, clockticks6502 - debugCPUClocks, 14, col_data);
}

// *******************************************************************************************
//
//									Render Top of Stack
//
// *******************************************************************************************

static void DEBUGRenderStack(int bytesCount) {
	uint16_t sp = regs.sp;
	increment_wrap_at_page_boundary(&sp);

	int y= 0;
	while (y < bytesCount) {
		DEBUGNumber(DBG_STCK,y, sp,4, col_label);
		int byte = debug_read6502(sp, USE_CURRENT_BANK);
		DEBUGNumber(DBG_STCK+5,y,byte,2, col_data);
		DEBUGWrite(dbgRenderer, DBG_STCK+9,y,byte, col_data);
		y++;
		increment_wrap_at_page_boundary(&sp);
	}
}

// *******************************************************************************************
//
//									Write Hex Constant
//
// *******************************************************************************************

static void DEBUGNumber(int x, int y, int n, int w, SDL_Color colour) {
	char fmtString[8],buffer[16];
	snprintf(fmtString, sizeof(fmtString), "%%0%dX", w);
	snprintf(buffer, sizeof(buffer), fmtString, n);
	DEBUGString(dbgRenderer, x, y, buffer, colour);
}

// *******************************************************************************************
//
//					Write Decimal Constant with thousands separator
//
// *******************************************************************************************

static void DEBUGNumberDec(int x, int y, int n, int w, SDL_Color colour) {
	char buf1[32], buf2[32];
	int i,j;
	snprintf(buf1, sizeof(buf1), "%d", n);
	buf2[sizeof(buf2)-1] = 0; // null terminate string
	int count = 0;
	for (i=strlen(buf1) - 1, j=sizeof(buf2) - 1; i >= 0 && j > 1; i--) {
		buf2[--j] = buf1[i];
		count++;

		if (count == 3) {
			buf2[--j] = ' ';
			count = 0;
		}
	}

	if (buf2[j] == ' ') j++;
	DEBUGString(dbgRenderer, x+(w-strlen(buf2+j)), y, buf2+j, colour);
}

// *******************************************************************************************
//
//									Write Bank:Address
//
// *******************************************************************************************
static void DEBUGAddress(int x, int y, int bank, int addr, SDL_Color colour) {
	char buffer[4];

	if(addr >= 0xA000) {
		snprintf(buffer, sizeof(buffer), "%.2X:", bank);
	} else {
		strcpy(buffer, "--:");
	}

	DEBUGString(dbgRenderer, x, y, buffer, colour);

	DEBUGNumber(x+3, y, addr, 4, colour);

}

static void
DEBUGVAddress(int x, int y, int addr, SDL_Color colour)
{
	DEBUGNumber(x, y, addr, 5, colour);
}
