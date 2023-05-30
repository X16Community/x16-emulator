#include <SDL.h>
#include <stdio.h>
#include <stdbool.h>
#include "glue.h"
#include "i2c.h"
#include "keyboard.h"

#define EXTENDED_FLAG 0x100
// #define ESC_IS_BREAK /* if enabled, Esc sends Break/Pause key instead of Esc */

int
keynum_from_SDL_Scancode(SDL_Scancode scancode)
{
	switch (scancode) {
		case SDL_SCANCODE_GRAVE:
			return 1;
		case SDL_SCANCODE_BACKSPACE:
			return 15;
		case SDL_SCANCODE_TAB:
			return 16;
		case SDL_SCANCODE_CLEAR:
			return 0;
		case SDL_SCANCODE_RETURN:
			return 43;
		case SDL_SCANCODE_PAUSE:
			return 126;
		case SDL_SCANCODE_ESCAPE:
#ifdef ESC_IS_BREAK
			return 126;
#else
			return 110;
#endif
		case SDL_SCANCODE_SPACE:
			return 61;
		case SDL_SCANCODE_APOSTROPHE:
			return 41;
		case SDL_SCANCODE_COMMA:
			return 53;
		case SDL_SCANCODE_MINUS:
			return 12;
		case SDL_SCANCODE_PERIOD:
			return 54;
		case SDL_SCANCODE_SLASH:
			return 55;
		case SDL_SCANCODE_0:
			return 11;
		case SDL_SCANCODE_1:
			return 2;
		case SDL_SCANCODE_2:
			return 3;
		case SDL_SCANCODE_3:
			return 4;
		case SDL_SCANCODE_4:
			return 5;
		case SDL_SCANCODE_5:
			return 6;
		case SDL_SCANCODE_6:
			return 7;
		case SDL_SCANCODE_7:
			return 8;
		case SDL_SCANCODE_8:
			return 9;
		case SDL_SCANCODE_9:
			return 10;
		case SDL_SCANCODE_SEMICOLON:
			return 40;
		case SDL_SCANCODE_EQUALS:
			return 13;
		case SDL_SCANCODE_LEFTBRACKET:
			return 27;
		case SDL_SCANCODE_BACKSLASH:
			return 29;
		case SDL_SCANCODE_RIGHTBRACKET:
			return 28;
		case SDL_SCANCODE_A:
			return 31;
		case SDL_SCANCODE_B:
			return 50;
		case SDL_SCANCODE_C:
			return 48;
		case SDL_SCANCODE_D:
			return 33;
		case SDL_SCANCODE_E:
			return 19;
		case SDL_SCANCODE_F:
			return 34;
		case SDL_SCANCODE_G:
			return 35;
		case SDL_SCANCODE_H:
			return 36;
		case SDL_SCANCODE_I:
			return 24;
		case SDL_SCANCODE_J:
			return 37;
		case SDL_SCANCODE_K:
			return 38;
		case SDL_SCANCODE_L:
			return 39;
		case SDL_SCANCODE_M:
			return 52;
		case SDL_SCANCODE_N:
			return 51;
		case SDL_SCANCODE_O:
			return 25;
		case SDL_SCANCODE_P:
			return 26;
		case SDL_SCANCODE_Q:
			return 17;
		case SDL_SCANCODE_R:
			return 20;
		case SDL_SCANCODE_S:
			return 32;
		case SDL_SCANCODE_T:
			return 21;
		case SDL_SCANCODE_U:
			return 23;
		case SDL_SCANCODE_V:
			return 49;
		case SDL_SCANCODE_W:
			return 18;
		case SDL_SCANCODE_X:
			return 47;
		case SDL_SCANCODE_Y:
			return 22;
		case SDL_SCANCODE_Z:
			return 46;
		case SDL_SCANCODE_DELETE:
			return 76;
		case SDL_SCANCODE_UP:
			return 83;
		case SDL_SCANCODE_DOWN:
			return 84;
		case SDL_SCANCODE_RIGHT:
			return 89;
		case SDL_SCANCODE_LEFT:
			return 79;
		case SDL_SCANCODE_INSERT:
			return 75;
		case SDL_SCANCODE_HOME:
			return 80;
		case SDL_SCANCODE_END:
			return 81;
		case SDL_SCANCODE_PAGEUP:
			return 85;
		case SDL_SCANCODE_PAGEDOWN:
			return 86;
		case SDL_SCANCODE_F1:
			return 112;
		case SDL_SCANCODE_F2:
			return 113;
		case SDL_SCANCODE_F3:
			return 114;
		case SDL_SCANCODE_F4:
			return 115;
		case SDL_SCANCODE_F5:
			return 116;
		case SDL_SCANCODE_F6:
			return 117;
		case SDL_SCANCODE_F7:
			return 118;
		case SDL_SCANCODE_F8:
			return 119;
		case SDL_SCANCODE_F9:
			return 120;
		case SDL_SCANCODE_F10:
			return 121;
		case SDL_SCANCODE_F11:
			return 122;
		case SDL_SCANCODE_F12:
			return 123;
		case SDL_SCANCODE_SCROLLLOCK:
			return 125;
		case SDL_SCANCODE_RSHIFT:
			return 57;
		case SDL_SCANCODE_LSHIFT:
			return 44;
		case SDL_SCANCODE_CAPSLOCK:
			return 30;
		case SDL_SCANCODE_LCTRL:
			return 58;
		case SDL_SCANCODE_RCTRL:
			return 64;
		case SDL_SCANCODE_LALT:
			return 60;
		case SDL_SCANCODE_RALT:
			return 62;
		case SDL_SCANCODE_LGUI:
			return 59;
		case SDL_SCANCODE_RGUI:
			return 63;
		case SDL_SCANCODE_APPLICATION: // Menu
			return 65;
		case SDL_SCANCODE_NONUSBACKSLASH:
			return 45;
		case SDL_SCANCODE_KP_ENTER:
			return 108;
		case SDL_SCANCODE_KP_0:
			return 99;
		case SDL_SCANCODE_KP_1:
			return 93;
		case SDL_SCANCODE_KP_2:
			return 98;
		case SDL_SCANCODE_KP_3:
			return 103;
		case SDL_SCANCODE_KP_4:
			return 92;
		case SDL_SCANCODE_KP_5:
			return 97;
		case SDL_SCANCODE_KP_6:
			return 102;
		case SDL_SCANCODE_KP_7:
			return 91;
		case SDL_SCANCODE_KP_8:
			return 96;
		case SDL_SCANCODE_KP_9:
			return 101;
		case SDL_SCANCODE_KP_PERIOD:
			return 104;
		case SDL_SCANCODE_KP_PLUS:
			return 106;
		case SDL_SCANCODE_KP_MINUS:
			return 105;
		case SDL_SCANCODE_KP_MULTIPLY:
			return 100;
		case SDL_SCANCODE_KP_DIVIDE:
			return 95;
		case SDL_SCANCODE_NUMLOCKCLEAR:
			return 90;
		default:
			return 0;
	}
}

void
handle_keyboard(bool down, SDL_Keycode sym, SDL_Scancode scancode)
{
	int keynum = keynum_from_SDL_Scancode(scancode);
	
	if (keynum == 0) return;

	if (down) {
		if (log_keyboard) {
			printf("DOWN 0x%02X\n", scancode);
			fflush(stdout);
		}

		if (keynum & EXTENDED_FLAG) {
			i2c_kbd_buffer_add(0x7f);
		}
		i2c_kbd_buffer_add(keynum & 0xff);
	} else {
		if (log_keyboard) {
			printf("UP   0x%02X\n", scancode);
			fflush(stdout);
		}

		keynum = keynum | 0b10000000;
		if (keynum & EXTENDED_FLAG) {
			i2c_kbd_buffer_add(0xff);
		}
		i2c_kbd_buffer_add(keynum & 0xff);
	}
}