// Commander X16 Emulator
// Copyright (c) 2021 Michael Steil
// All rights reserved. License: 2-clause BSD

#ifndef _RTC_H_
#define _RTC_H_

#include <stdint.h>

void rtc_i2c_data(uint8_t v);
void rtc_init(bool set_system_time);
void rtc_set_system_time();
void rtc_step(int c);
uint8_t rtc_read();
void rtc_write();

#endif
