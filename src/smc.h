// Commander X16 Emulator
// Copyright (c) 2021 Michael Steil
// All rights reserved. License: 2-clause BSD

#ifndef _SMC_H_
#define _SMC_H_

#define SMC_VERSION_MAJOR 47
#define SMC_VERSION_MINOR 0
#define SMC_VERSION_PATCH 0

#include <stdint.h>

extern void nmi6502();
void smc_i2c_data(uint8_t v);
uint8_t smc_read();
void smc_write();

extern bool smc_requested_reset;

#endif
