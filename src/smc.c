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

uint8_t default_read_op = 0x41;
uint8_t default_read_state = 0;
uint8_t activity_led;
uint8_t mse_count = 0;
#define I2C_DATA_LEN 16
static uint8_t i2c_data[I2C_DATA_LEN];
static uint8_t i2c_data_pos = 0;
bool smc_requested_reset = false;

void
smc_i2c_data(uint8_t v) {
	if (i2c_data_pos < I2C_DATA_LEN) {
		i2c_data[i2c_data_pos] = v;
		i2c_data_pos++;
	}
}

uint8_t
smc_read() {
	uint8_t mouse_id;
	uint8_t mouse_size;
	uint8_t ret;

	switch (i2c_data[0]){
		case 0x43: // keycode + mouse
			if (default_read_state == 0) {
				default_read_state = 1;
				ret = i2c_kbd_buffer_next();
				break;
			}
			// fall through
		case 0x42: // mouse only
		// fall through
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
				ret = i2c_mse_buffer_next();
			}
			else if (mse_count > 0) {								// If we have already started sending bytes, assume there is enough data in the buffer
				mse_count++;
				if (mse_count == mouse_size) {
					mse_count = 0;
					default_read_state = 0;
				}
				ret = i2c_mse_buffer_next();
			}
			else {													// Return a single zero if no complete packet available
				mse_count = 0;
				default_read_state = 0;
				ret = 0x00;
			}
			break;
		case 0x41: // keycode only
			default_read_state = 0;
			// fall through
		// Offset that returns one byte from the keyboard buffer
		case 0x07:
			ret = i2c_kbd_buffer_next();
			break;
		// Offset, initially used for debugging in the SMC, but now
		// also used to inform kernal whether power on was done by
		// holding the power button
		case 0x09:
			if (pwr_long_press) {
				// Reset pwr_long_press so a reset will start normally
				pwr_long_press = false;
				ret = 1;
			} else {
				ret = 0;
			}
			break;

		case 0x22:
			ret = mouse_get_device_id();
			break;

		case 0x30:
			ret = SMC_VERSION_MAJOR;
			break;

		case 0x31:
			ret = SMC_VERSION_MINOR;
			break;

		case 0x32:
			ret = SMC_VERSION_PATCH;
			break;

		default:
			ret = 0xff;
	}

	i2c_data[0] = default_read_op;
	i2c_data_pos = 0;
	return ret;
}

void
smc_write() {
	switch (i2c_data[0]) {
		case 1:
			if (i2c_data[1] == 0) {
				printf("SMC Power Off.\n");
				main_shutdown();
#ifdef __EMSCRIPTEN__
				emscripten_force_exit(0);
#endif
				exit(0);
			} else if (i2c_data[1] == 1) {
				smc_requested_reset = true;
			}
			break;
		case 2:
			if (i2c_data[1] == 0) {
				smc_requested_reset = true;
			}
			break;
		case 3:
			if (i2c_data[1] == 0) {
				nmi6502();
			}
			break;
		case 4:
			// Power LED is not controllable
			break;
		case 5:
			activity_led = i2c_data[1] >= 128 ? 255 : 0;
			break;

		case 0x20:
			mouse_set_device_id(i2c_data[1]);
			break;

		case 0x40:
			default_read_op = i2c_data[1];
			default_read_state = 0;
			break;
	}

	i2c_data[0] = default_read_op;
	i2c_data_pos = 0;
}

