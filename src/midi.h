// Commander X16 Emulator
// Copyright (c) 2024 MooingLemur
// All rights reserved. License: 2-clause BSD

#pragma once

#ifndef __EMSCRIPTEN__
#include <fluidsynth.h>
#endif

void midi_init();
uint8_t midi_serial_read(uint8_t reg, bool debugOn);
void midi_serial_write(uint8_t reg, uint8_t val);
void midi_load_sf2(uint8_t* filename);
