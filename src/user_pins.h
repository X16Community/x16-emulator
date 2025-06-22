#pragma once

#include <stdint.h>

// This will be passed to the user-port library's [user_port_init] function, so
// it can give an error on version mismatch, rather than (probably) segfault or
// behave strangely.
#define X16_USER_PORT_API_VERSION 1

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

typedef uint32_t user_pin_t;

typedef struct {
	user_pin_t connected_pins;
	user_pin_t (*read)();
	void (*write)(user_pin_t pins);
	user_pin_t (*step)(double nanos);
} user_port_t;

typedef int (*user_port_init_t)(int api_version, user_port_t *);
