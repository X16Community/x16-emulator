#pragma once

// Include this header in a user peripheral library.
//
// A user peripheral library must export a [user_port_init_t x16_user_port_init] to be
// called at runtime by the emulator during initialization of via2. [x16_user_port_init]
// should return 0 on success, and -1 on error.

#include <stdint.h>

// This must be assigned to the [api_version] field of [user_port_t] by the peripheral
// library so that version mismatches can be detected.
#define X16_USER_PORT_API_VERSION 1

// Bit assignments for each 65C22 pin exposed to the user port, for use with
// [user_pin_t]. For convenience, all Port A pins are in the low byte, in bit order, and
// Port B pins are likewise in the second byte.

#define PA0_PIN (1 << 0)
#define PA1_PIN (1 << 1)
#define PA2_PIN (1 << 2)
#define PA3_PIN (1 << 3)
#define PA4_PIN (1 << 4)
#define PA5_PIN (1 << 5)
#define PA6_PIN (1 << 6)
#define PA7_PIN (1 << 7)
#define PB0_PIN (1 << 8)
#define PB1_PIN (1 << 9)
#define PB2_PIN (1 << 10)
#define PB3_PIN (1 << 11)
#define PB4_PIN (1 << 12)
#define PB5_PIN (1 << 13)
#define PB6_PIN (1 << 14)
#define PB7_PIN (1 << 15)
#define CA1_PIN (1 << 16)
#define CA2_PIN PB3_PIN
#define CB1_PIN PB6_PIN
#define CB2_PIN PB7_PIN

// USER_PINn macros map the 65C22 pins (above) to the exposed pins on the X16's user port.

// Left column
#define USER_PIN1 PB0_PIN
#define USER_PIN3 PA0_PIN
#define USER_PIN5 PA1_PIN
#define USER_PIN7 PA2_PIN
#define USER_PIN9 PA3_PIN
#define USER_PIN11 PA4_PIN
#define USER_PIN13 PA5_PIN
#define USER_PIN15 PA6_PIN
#define USER_PIN17 PA7_PIN
#define USER_PIN19 CA1_PIN
#define USER_PIN21 PB1_PIN
#define USER_PIN23 PB2_PIN
#define USER_PIN25 PB3_PIN

// Right Column
#define USER_PIN2 PB4_PIN
#define USER_PIN4 PB5_PIN
#define USER_PIN6 PB6_PIN
#define USER_PIN8 PB7_PIN
// 10-24 (even) are GND, 26 is VCC

// [user_pin_t] is a bitmask of pin values. Port A is the first byte, Port B is the second
// byte, and CA1 is bit 16. Bits above 16 must always be 0. The above *_PIN macros define
// the appropriate bits to use with [user_pin_t].
//
// Note that in this implementation, all pins are either high (1) or low (0). If your
// device uses pull-up/down, you'll need to factor that into [read] or [step] pin values
// manually. However, unless the data direction of the pins on the via stays constant it
// will be quite difficult to keep the via's and peripheral's states in sync, since there
// is no signal that a via pin has switched from being actively driven to hi-Z, or vice
// versa.
typedef uint32_t user_pin_t;

typedef struct user_port_t {
	// Must be set to X16_USER_PORT_API_VERSION
	int api_version;

	// A mask of pins actually connected to the user peripheral.
	user_pin_t connected_pins;

	// Return the values of the connected pins based on the peripheral's internal
	// state. Any pin values not in [connected_pins] will be ignored.
	user_pin_t (*read)(void);

	// New pin values pushed from the via to the peripheral. Pins not in the
	// [connected_pins] mask will be zeroes, but that does not imply those pins are low
	void (*write)(user_pin_t pins);

	// Step the state machine of the connected peripheral. [nanos] is the number of
	// nanoseconds which has passed since the last step. Returns the pin state so that any
	// interrupts based on CA1 and CB1 can be triggered. CA2 and CB2 are not presently
	// implemented in the via code.
	user_pin_t (*step)(double nanos);
} user_port_t;

// Extensible init args struct. Includes api_version so perhipheral libraries can abort or
// downgrade, if necessary.
typedef struct __attribute__((aligned(sizeof(void *)))) user_port_init_args_t {
	int api_version;
} user_port_init_args_t;

// Populates the provided [user_port_t *]. If any of [read], [write] or [step] is NULL, it
// will be ignored.
//
// A peripheral library's exposed [user_port_init_t] function MUST be named "x16_user_port_init".
//
// Returns 0 on success and <0 on error.
typedef int (*user_port_init_t)(user_port_init_args_t *, user_port_t *);
