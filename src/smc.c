// Commander X16 Emulator
// Copyright (c) 2021 Michael Steil
// All rights reserved. License: 2-clause BSD

// System Management Controller

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include "smc.h"
#include "glue.h"
#include "i2c.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

// 0x01 0x00      - Power Off
// 0x01 0x01      - Hard Reboot
// 0x02 0x00      - Reset Button Press
// 0x03 0x00      - NMI Button Press
// 0x04 0x00-0xFF - Power LED Level (PWM)
// 0x05 0x00-0xFF - Activity LED Level (PWM)

uint8_t activity_led;
uint8_t mse_count = 0;
bool smc_requested_reset = false;

uint8_t
smc_read(uint8_t a) {
	uint8_t mouse_id;
	uint8_t mouse_size;

	switch (a){
		// Offset that returns one byte from the keyboard buffer
		case 7:
			return i2c_kbd_buffer_next();
		// Offset, initially used for debugging in the SMC, but now
		// also used to inform kernal whether power on was done by
		// holding the power button
		case 9:
			if (pwr_long_press) {
				// Reset pwr_long_press so a reset will start normally
				pwr_long_press = false;
				return 1;
			} else {
				return 0;
			}
		// Offset that returns three bytes from mouse buffer (one movement packet) or a single zero if there is not complete packet in the buffer
		// mse_count keeps track of which one of the three bytes it's sending
		case 0x21:
			mouse_id = mouse_get_device_id();
			if (mouse_id == 3 || mouse_id == 4) {
				mouse_size = 4;
			}
			else {
				mouse_size = 3;
			}

			if (mse_count == 0 && i2c_mse_buffer_count() >= mouse_size) {		// If start of packet, check if there are at least three bytes in the buffer
				mse_count++;
				return i2c_mse_buffer_next();
			}
			else if (mse_count > 0) {								// If we have already started sending bytes, assume there is enough data in the buffer
				mse_count++;
				if (mse_count == mouse_size) mse_count = 0;
				return i2c_mse_buffer_next();
			}
			else {													// Return a single zero if no complete packet available
				mse_count = 0;
				return 0x00;
			}

		case 0x22:
			return mouse_get_device_id();

		default:
			return 0xff;
	}
}

void
smc_write(uint8_t a, uint8_t v) {
	switch (a) {
		case 1:
			if (v == 0) {
				printf("SMC Power Off.\n");
				main_shutdown();
#ifdef __EMSCRIPTEN__
				emscripten_force_exit(0);
#endif
				exit(0);
			} else if (v == 1) {
				smc_requested_reset = true;
			}
			break;
		case 2:
			if (v == 0) {
				smc_requested_reset = true;
			}
			break;
		case 3:
			if (v == 0) {
				nmi6502();
			}
			break;
		case 4:
			// Power LED is not controllable
			break;
		case 5:
			activity_led = v >= 128 ? 255 : 0;
			break;

		case 0x20:
			mouse_set_device_id(v);
			break;
	}
}

