// Commander X16 Emulator
// Copyright (c) 2024 MooingLemur
// All rights reserved. License: 2-clause BSD

#include <pthread.h>
#include "glue.h"
#include "midi.h"

#ifdef _WIN32
    #include <windows.h>
    #define LIBRARY_TYPE HMODULE
    #define LOAD_LIBRARY(name) LoadLibrary(name)
    #define GET_FUNCTION(lib, name) GetProcAddress(lib, name)
    #define CLOSE_LIBRARY(lib) FreeLibrary(lib)
#else
    #include <dlfcn.h>
    #define LIBRARY_TYPE void*
    #define LOAD_LIBRARY(name) dlopen(name, RTLD_LAZY)
    #define GET_FUNCTION(lib, name) dlsym(lib, name)
    #define CLOSE_LIBRARY(lib) dlclose(lib)
#endif

#define ASSIGN_FUNCTION(lib, var, name) {\
    var = (void *)GET_FUNCTION(lib, name);\
    if (!var) { fprintf(stderr, "Unable to find symbol for '%s'\n", name); CLOSE_LIBRARY(handle); return; }\
}

enum MIDI_states {
    NORMAL,
    PARAM,
    SYSEX,
};

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define LOW_HIGH_UNION(name, low, high) \
    union { \
        struct { \
            uint8_t low; \
            uint8_t high; \
        }; \
        uint16_t name; \
    }
#else
#define LOW_HIGH_UNION(name, low, high) \
    union { \
        struct { \
            uint8_t high; \
            uint8_t low; \
        }; \
        uint16_t name; \
    }
#endif

struct midi_serial_regs
{
    LOW_HIGH_UNION(dl, dll, dlm);

    bool ier_erbi;
    bool ier_etbei;
    bool ier_elsi;
    bool ier_edssi;

    uint8_t iir;

    bool fcr_fifo_enable;
    uint8_t fcr_ififo_trigger_level_bytes;

    uint8_t lcr_word_length_bits;
    bool lcr_stb;
    bool lcr_pen;
    bool lcr_eps;
    bool lcr_stick;
    bool lcr_break;
    bool lcr_dlab;

    bool mcr_dtr;
    bool mcr_rts;
    bool mcr_out1;
    bool mcr_out2;
    bool mcr_loop;
    bool mcr_afe;

    bool lsr_oe;
    bool lsr_pe;
    bool lsr_fe;
    bool lsr_bi;
    bool lsr_eif;

    bool msr_dcts;
    bool msr_ddsr;
    bool msr_teri;
    bool msr_ddcd;
    bool msr_cts;
    bool msr_dsr;
    bool msr_ri;
    bool msr_dcd;

    uint8_t obyte_bits_remain;
    uint8_t rx_timeout;
    bool rx_timeout_enabled;

    bool thre_intr;
    uint8_t thre_bits_remain;

    uint8_t scratch;
    uint8_t ififo[16];
    uint8_t ifsz;
    uint8_t ofifo[16];
    uint8_t ofsz;

    int64_t clock; // 40.24 fixed point
    int32_t clockdec; // 8.24 fixed point

    time_t last_warning;

    pthread_mutex_t fifo_mutex;
    pthread_mutexattr_t fifo_mutex_attr;
};

#undef LOW_HIGH_UNION

struct midi_serial_regs mregs[2];

#ifndef __EMSCRIPTEN__

static uint8_t sysex_buffer[1024];
static int sysex_bufptr;

static enum MIDI_states midi_state = NORMAL;
static uint8_t midi_last_command = 0;
static uint8_t midi_first_param;

static bool midi_initialized = false;
static bool serial_midi_mutexes_initialized = false;

static fluid_settings_t* fl_settings;
//static fluid_midi_driver_t* fl_mdriver;
static fluid_audio_driver_t* fl_adriver;
static fluid_synth_t* fl_synth;
static int fl_sf2id;

typedef fluid_settings_t* (*new_fluid_settings_f_t)(void);
typedef fluid_synth_t* (*new_fluid_synth_f_t)(fluid_settings_t*);
typedef fluid_audio_driver_t* (*new_fluid_audio_driver_f_t)(fluid_settings_t*, fluid_synth_t*);
typedef int (*fluid_synth_sfload_f_t)(fluid_synth_t*,const char *,int);
typedef int (*fluid_synth_program_change_f_t)(fluid_synth_t*, int, int);
typedef int (*fluid_synth_channel_pressure_f_t)(fluid_synth_t*, int, int);
typedef int (*fluid_synth_system_reset_f_t)(fluid_synth_t*);
typedef int (*fluid_synth_noteoff_f_t)(fluid_synth_t*, int, int);
typedef int (*fluid_synth_noteon_f_t)(fluid_synth_t*, int, int, int);
typedef int (*fluid_synth_key_pressure_f_t)(fluid_synth_t*, int, int, int);
typedef int (*fluid_synth_cc_f_t)(fluid_synth_t*, int, int, int);
typedef int (*fluid_synth_pitch_bend_f_t)(fluid_synth_t*, int, int);
typedef int (*fluid_synth_sysex_f_t)(fluid_synth_t*, const char*, int, char*, int*, int*, int);

static new_fluid_settings_f_t dl_new_fluid_settings;
static new_fluid_synth_f_t dl_new_fluid_synth;
static new_fluid_audio_driver_f_t dl_new_fluid_audio_driver;
static fluid_synth_sfload_f_t dl_fs_sfload;
static fluid_synth_program_change_f_t dl_fs_program_change;
static fluid_synth_channel_pressure_f_t dl_fs_channel_pressure;
static fluid_synth_system_reset_f_t dl_fs_system_reset;
static fluid_synth_noteoff_f_t dl_fs_noteoff;
static fluid_synth_noteon_f_t dl_fs_noteon;
static fluid_synth_key_pressure_f_t dl_fs_key_pressure;
static fluid_synth_cc_f_t dl_fs_cc;
static fluid_synth_pitch_bend_f_t dl_fs_pitch_bend;
static fluid_synth_sysex_f_t dl_fs_sysex;

void midi_init()
{
    if (midi_initialized) {
        return;
    }

#ifdef _WIN32
    LIBRARY_TYPE handle = LOAD_LIBRARY("libfluidsynth-3.dll");
#elif __APPLE__
    LIBRARY_TYPE handle = LOAD_LIBRARY("libfluidsynth.dylib");
#else
    LIBRARY_TYPE handle = LOAD_LIBRARY("libfluidsynth.so");
#endif

    if (!handle) {
        // Handle the error on both platforms
#ifdef _WIN32
        fprintf(stderr, "Could not load MIDI synth library: error code %lu\n", GetLastError());
#else
        fprintf(stderr, "Could not load MIDI synth library: %s\n", dlerror());
#endif
        return;
    }

#ifndef _WIN32
    dlerror();
#endif

    ASSIGN_FUNCTION(handle, dl_new_fluid_settings, "new_fluid_settings");
    ASSIGN_FUNCTION(handle, dl_new_fluid_synth, "new_fluid_synth");
    ASSIGN_FUNCTION(handle, dl_new_fluid_audio_driver, "new_fluid_audio_driver");
    ASSIGN_FUNCTION(handle, dl_fs_sfload, "fluid_synth_sfload");
    ASSIGN_FUNCTION(handle, dl_fs_program_change, "fluid_synth_program_change");
    ASSIGN_FUNCTION(handle, dl_fs_channel_pressure, "fluid_synth_channel_pressure");
    ASSIGN_FUNCTION(handle, dl_fs_system_reset, "fluid_synth_system_reset");
    ASSIGN_FUNCTION(handle, dl_fs_noteoff, "fluid_synth_noteoff");
    ASSIGN_FUNCTION(handle, dl_fs_noteon, "fluid_synth_noteon");
    ASSIGN_FUNCTION(handle, dl_fs_key_pressure, "fluid_synth_key_pressure");
    ASSIGN_FUNCTION(handle, dl_fs_cc, "fluid_synth_cc");
    ASSIGN_FUNCTION(handle, dl_fs_pitch_bend, "fluid_synth_pitch_bend");
    ASSIGN_FUNCTION(handle, dl_fs_sysex, "fluid_synth_sysex");

    fl_settings = dl_new_fluid_settings();
    fl_synth = dl_new_fluid_synth(fl_settings);
    fl_adriver = dl_new_fluid_audio_driver(fl_settings, fl_synth);

    midi_initialized = true;
    printf("Initialized MIDI synth.\n");
}

void midi_load_sf2(uint8_t* filename)
{
    if (!midi_initialized) return;
    fl_sf2id = dl_fs_sfload(fl_synth, (const char *)filename, true);
    if (fl_sf2id == FLUID_FAILED) {
        printf("Unable to load soundfont.\n");
    }
}

// Receive a byte from client
// Store state, or dispatch event
void midi_byte(uint8_t b)
{
    if (!midi_initialized) return;
    switch (midi_state) {
        case NORMAL:
            if (b < 0x80) {
                if ((midi_last_command & 0xf0) == 0xc0) { // patch change
                    dl_fs_program_change(fl_synth, midi_last_command & 0xf, b);
                } else if ((midi_last_command & 0xf0) == 0xd0) { // channel pressure
                    dl_fs_channel_pressure(fl_synth, midi_last_command & 0xf, b);
                } else if (midi_last_command >= 0x80) { // two-param command
                    midi_first_param = b;
                    midi_state = PARAM;
                }
            } else {
                if (b < 0xf0) {
                    midi_last_command = b;
                } else if (b == 0xf0) {
                    sysex_bufptr = 0;
                    midi_state = SYSEX;
                } else if (b == 0xff) {
                    dl_fs_system_reset(fl_synth);
                    midi_last_command = 0;
                }
            }
            break;
        case PARAM:
            switch (midi_last_command & 0xf0) {
                case 0x80: // note off
                    dl_fs_noteoff(fl_synth, midi_last_command & 0xf, midi_first_param); // no release velocity
                    break;
                case 0x90: // note on
                    if (b == 0) {
                        dl_fs_noteoff(fl_synth, midi_last_command & 0xf, midi_first_param);
                    } else {
                        dl_fs_noteon(fl_synth, midi_last_command & 0xf, midi_first_param, b);
                    }
                    break;
                case 0xa0: // aftertouch
                    dl_fs_key_pressure(fl_synth, midi_last_command & 0xf, midi_first_param, b);
                    break;
                case 0xb0: // controller
                    dl_fs_cc(fl_synth, midi_last_command & 0xf, midi_first_param, b);
                    break;
                case 0xe0: // pitch bend
                    dl_fs_pitch_bend(fl_synth, midi_last_command & 0xf, ((uint16_t)midi_first_param) | (uint16_t)b << 7);
                    break;
            }
            midi_state = NORMAL;
            break;
        case SYSEX:
            if (b == 0xf7) {
                if (sysex_bufptr < (sizeof(sysex_buffer) / sizeof(sysex_buffer[0]))-1) { // only if buffer didn't fill
                    sysex_buffer[sysex_bufptr] = 0;
                    dl_fs_sysex(fl_synth, (const char *)sysex_buffer, sysex_bufptr, NULL, NULL, NULL, 0);
                }
                midi_state = NORMAL;
            } else {
                sysex_buffer[sysex_bufptr] = b;
                // we can't do much about a runaway sysex other than continue to absorb it
                // but we throw it all away later if the buffer filled
                if (sysex_bufptr < (sizeof(sysex_buffer) / sizeof(sysex_buffer[0]))-1) {
                    sysex_bufptr++;
                }
            }
            break;
    }
}

#else
void midi_load_sf2(uint8_t* filename)
{
    // no-op
}

void midi_init()
{
    fprintf(stderr, "No FluidSynth support.\n");
}

void midi_byte(uint8_t b)
{
    // no-op
}

#endif

void midi_serial_init()
{
    uint8_t sel;

    for (sel=0; sel<2; sel++) {
        mregs[sel].ier_erbi = false;
        mregs[sel].ier_etbei = false;
        mregs[sel].ier_elsi = false;
        mregs[sel].ier_edssi = false;

        mregs[sel].iir = 0x01;

        mregs[sel].fcr_fifo_enable = false;
        mregs[sel].fcr_ififo_trigger_level_bytes = 1;

        mregs[sel].lcr_word_length_bits = 5;
        mregs[sel].lcr_stb = false;
        mregs[sel].lcr_pen = false;
        mregs[sel].lcr_eps = false;
        mregs[sel].lcr_stick = false;
        mregs[sel].lcr_break = false;
        mregs[sel].lcr_dlab = false;

        mregs[sel].mcr_dtr = false;
        mregs[sel].mcr_rts = false;
        mregs[sel].mcr_out1 = false;
        mregs[sel].mcr_out2 = false;
        mregs[sel].mcr_loop = false;
        mregs[sel].mcr_afe = false;

        mregs[sel].lsr_oe = false;
        mregs[sel].lsr_pe = false;
        mregs[sel].lsr_fe = false;
        mregs[sel].lsr_bi = false;
        mregs[sel].lsr_eif = false;

        mregs[sel].msr_dcts = false;
        mregs[sel].msr_ddsr = false;
        mregs[sel].msr_teri = false;
        mregs[sel].msr_ddcd = false;

        mregs[sel].msr_cts = false;
        mregs[sel].msr_dsr = false;
        mregs[sel].msr_ri = false;
        mregs[sel].msr_dcd = false;

        mregs[sel].obyte_bits_remain = 0;
        mregs[sel].rx_timeout = 0;
        mregs[sel].rx_timeout_enabled = false;
        mregs[sel].thre_intr = false;
        mregs[sel].thre_bits_remain = 0;

        mregs[sel].ifsz = 0;
        mregs[sel].ofsz = 0;
        mregs[sel].clock = 0;
        mregs[sel].clockdec = 0;
        mregs[sel].last_warning = 0;

    }

    if (!serial_midi_mutexes_initialized) {
        for (sel=0; sel<2; sel++) {
            pthread_mutexattr_init(&mregs[sel].fifo_mutex_attr);
            pthread_mutex_init(&mregs[sel].fifo_mutex, &mregs[sel].fifo_mutex_attr);
        }
        serial_midi_mutexes_initialized = true;
    }
}

void midi_serial_step(int clocks)
{
    uint8_t sel, i;
    for (sel=0; sel<2; sel++) {
        mregs[sel].clock -= (int64_t)mregs[sel].clockdec * clocks;
        while (mregs[sel].clock < 0) {
            // process uart
            pthread_mutex_lock(&mregs[sel].fifo_mutex);
            if (mregs[sel].obyte_bits_remain > 0) {
                mregs[sel].obyte_bits_remain--;
            }
            if (mregs[sel].ofsz > 0) {
                if (mregs[sel].obyte_bits_remain == 0) {
                    if (sel == 1) {
                        midi_byte(mregs[sel].ofifo[0]);
                    }
                    mregs[sel].ofsz--;
                    if (mregs[sel].ofsz > 0) {
                        for (i=0; i<mregs[sel].ofsz; i++) {
                            mregs[sel].ofifo[i] = mregs[sel].ofifo[i+1];
                        }
                    } else if (mregs[sel].thre_bits_remain == 0) {
                        mregs[sel].thre_intr = true;
                    }
                    mregs[sel].obyte_bits_remain = 2 + mregs[sel].lcr_word_length_bits + mregs[sel].lcr_stb + mregs[sel].lcr_pen;
                }
            } else {
                if (mregs[sel].thre_bits_remain > 0) {
                    mregs[sel].thre_bits_remain--;
                    if (mregs[sel].thre_bits_remain == 0) {
                        mregs[sel].thre_intr = true;
                    }
                }
            }
            pthread_mutex_unlock(&mregs[sel].fifo_mutex);
            mregs[sel].clock += 0x1000000LL;
        }
    }
}

void midi_serial_enqueue_obyte(uint8_t sel, uint8_t val) {
    pthread_mutex_lock(&mregs[sel].fifo_mutex);
    if (mregs[sel].ofsz < (mregs[sel].fcr_fifo_enable ? 16 : 1)) {
        mregs[sel].ofifo[mregs[sel].ofsz] = val;
        mregs[sel].thre_intr = false;
        mregs[sel].ofsz++;
        if (mregs[sel].ofsz == 1) { // We were empty
            if (mregs[sel].fcr_fifo_enable) {
                // We delay a bit longer to reraise the THRE interrupt if we're coming from 0
                mregs[sel].thre_bits_remain = 1 + mregs[sel].lcr_word_length_bits + mregs[sel].lcr_stb + mregs[sel].lcr_pen;
            }
        } else {
            mregs[sel].thre_bits_remain = 0;
        }
    } else {
        printf("TX Overflow\n");
    }
    pthread_mutex_unlock(&mregs[sel].fifo_mutex);
}

void midi_serial_iir_check(uint8_t sel) {
    uint8_t fifoen = (uint8_t)mregs[sel].fcr_fifo_enable << 6 | (uint8_t)mregs[sel].fcr_fifo_enable << 7;
    if (mregs[sel].ier_elsi && (mregs[sel].lsr_oe || mregs[sel].lsr_pe || mregs[sel].lsr_fe || mregs[sel].lsr_bi)) {
        mregs[sel].iir = (0x06 | fifoen); // Receiver line status interrupt
    } else if (mregs[sel].ier_erbi && !mregs[sel].fcr_fifo_enable && mregs[sel].ifsz > 0) {
        mregs[sel].iir = (0x04 | fifoen); // Received data available interrupt (16450 mode)
    } else if (mregs[sel].ier_erbi && mregs[sel].fcr_fifo_enable && mregs[sel].ifsz >= mregs[sel].fcr_ififo_trigger_level_bytes) {
        mregs[sel].iir = (0x04 | fifoen); // Received data available interrupt (16550 mode)
    } else if (mregs[sel].ier_erbi && mregs[sel].fcr_fifo_enable && mregs[sel].ifsz > 0 && mregs[sel].rx_timeout_enabled && mregs[sel].rx_timeout == 0) {
        mregs[sel].iir = (0x0c | fifoen); // Received data available interrupt (timeout, 16550 mode)
    } else if (mregs[sel].ier_etbei && mregs[sel].thre_intr) {
        mregs[sel].iir = (0x02 | fifoen); // Transmitter holding register empty interrupt
    } else if (mregs[sel].ier_edssi && ((!mregs[sel].mcr_afe && mregs[sel].msr_dcts) || mregs[sel].msr_ddcd || mregs[sel].msr_ddsr || mregs[sel].msr_teri)) {
        mregs[sel].iir = (0x00 | fifoen); // Modem status register interrupt
    } else {
        mregs[sel].iir = (0x01 | fifoen); // no interrupt waiting
    }
}

uint8_t midi_serial_read(uint8_t reg, bool debugOn)
{
    //printf("midi_serial_read %d\n", reg);
    uint8_t sel = (reg & 8) >> 3;
    switch (reg & 7) {
        case 0x0:
            if (mregs[sel].lcr_dlab) {
                return mregs[sel].dll;
            } else {
                // TODO: RHR
            }
            break;
        case 0x1:
            if (mregs[sel].lcr_dlab) {
                return mregs[sel].dlm;
            } else {
                return (((uint8_t)mregs[sel].ier_edssi << 3) |
                        ((uint8_t)mregs[sel].ier_elsi << 2) |
                        ((uint8_t)mregs[sel].ier_etbei << 1) |
                        ((uint8_t)mregs[sel].ier_erbi));
            }
            break;
        case 0x2: {
            uint8_t ret = mregs[sel].iir;
            if (!debugOn) {
                mregs[sel].thre_intr = false;
                midi_serial_iir_check(sel);
            }
            return ret;
            break;
        }
        case 0x3:
            return (((uint8_t)mregs[sel].lcr_dlab << 7) |
                    ((uint8_t)mregs[sel].lcr_break << 6) |
                    ((uint8_t)mregs[sel].lcr_stick << 5) |
                    ((uint8_t)mregs[sel].lcr_eps << 4) |
                    ((uint8_t)mregs[sel].lcr_pen << 3) |
                    ((uint8_t)mregs[sel].lcr_stb << 2) |
                    ((mregs[sel].lcr_word_length_bits - 5) & 0x3));
            break;
        case 0x4:
            return (((uint8_t)mregs[sel].mcr_afe << 5) |
                    ((uint8_t)mregs[sel].mcr_loop << 4) |
                    ((uint8_t)mregs[sel].mcr_out2 << 3) |
                    ((uint8_t)mregs[sel].mcr_out1 << 2) |
                    ((uint8_t)mregs[sel].mcr_rts << 1) |
                    ((uint8_t)mregs[sel].mcr_dtr));
            break;
        case 0x5: {
            uint8_t ret = (((uint8_t)mregs[sel].lsr_eif << 7) |
                    ((uint8_t)(mregs[sel].obyte_bits_remain == 0 && mregs[sel].ofsz == 0) << 6) |
                    ((uint8_t)(mregs[sel].ofsz == 0) << 5) |
                    ((uint8_t)mregs[sel].lsr_bi << 4) |
                    ((uint8_t)mregs[sel].lsr_fe << 3) |
                    ((uint8_t)mregs[sel].lsr_pe << 2) |
                    ((uint8_t)mregs[sel].lsr_oe << 1) |
                    ((uint8_t)(mregs[sel].ifsz > 0)));
            if (!debugOn) {
                mregs[sel].lsr_oe = false;
                mregs[sel].lsr_pe = false;
                mregs[sel].lsr_fe = false;
                mregs[sel].lsr_bi = false;
            }
            return ret;
            break;
        }
        case 0x6:
            return (((uint8_t)mregs[sel].msr_dcd << 7) |
                    ((uint8_t)mregs[sel].msr_ri << 6) |
                    ((uint8_t)mregs[sel].msr_dsr << 5) |
                    ((uint8_t)mregs[sel].msr_cts << 4) |
                    ((uint8_t)mregs[sel].msr_ddcd << 3) |
                    ((uint8_t)mregs[sel].msr_teri << 2) |
                    ((uint8_t)mregs[sel].msr_ddsr << 1) |
                    ((uint8_t)mregs[sel].msr_dcts));
            break;
        case 0x7:
            return mregs[sel].scratch;
            break;
    }
    return 0x00;
}

void midi_serial_calculate_clk(uint8_t sel)
{
    double uart_clks_per_cpu = (MIDI_UART_OSC_RATE_MHZ / MIDI_UART_PRIMARY_DIVIDER) / MHZ;
    if (mregs[sel].dl > 0) {
        uart_clks_per_cpu /= mregs[sel].dl;
        mregs[sel].clockdec = uart_clks_per_cpu * 0x1000000L; // convert to 8.24 fixed point
    } else {
        mregs[sel].clockdec = 0;
    }
}

void midi_serial_write(uint8_t reg, uint8_t val)
{
    //printf("midi_serial_write %d %d\n", reg, val);
    uint8_t sel = (reg & 8) >> 3;
    switch (reg & 7) {
        case 0x0:
            if (mregs[sel].lcr_dlab) {
                mregs[sel].dll = val;
                midi_serial_calculate_clk(sel);
            } else {
                if (mregs[sel].lcr_word_length_bits != 8 || mregs[sel].lcr_stb || mregs[sel].lcr_pen || mregs[sel].lcr_eps || mregs[sel].lcr_stick || mregs[sel].lcr_break) {
                    if (mregs[sel].last_warning + 60 < time(NULL)) {
                        unsigned char par = 'N';
                        if (mregs[sel].lcr_pen) {
                            switch ((uint8_t)(mregs[sel].lcr_eps << 1) | (uint8_t)mregs[sel].lcr_stick) {
                                case 0:
                                    par = 'O';
                                    break;
                                case 1:
                                    par = 'M';
                                    break;
                                case 2:
                                    par = 'E';
                                    break;
                                case 3:
                                    par = 'S';
                                    break;
                            }
                        }
                        fprintf(stderr, "Serial MIDI: Warning: improper LCR %d%c%d for UART %d, must be set to 8N1.\n", mregs[sel].lcr_word_length_bits, par, 1+(uint8_t)mregs[sel].lcr_stb, sel);
                        mregs[sel].last_warning = time(NULL);
                    }
                } else if (mregs[sel].dl != 32 && mregs[sel].last_warning + 60 < time(NULL)) {
                    fprintf(stderr, "Serial MIDI: Warning: improper divisor %d for UART %d, must be set to 32 for standard MIDI bitrate.\n", mregs[sel].dl, sel);
                    mregs[sel].last_warning = time(NULL);
                } else {
                    midi_serial_enqueue_obyte(sel, val);
                }
            }
            break;
        case 0x1:
            if (mregs[sel].lcr_dlab) {
                mregs[sel].dlm = val;
                midi_serial_calculate_clk(sel);
            } else {
                mregs[sel].ier_erbi = !!(val & 1);
                mregs[sel].ier_etbei = !!(val & 2);
                mregs[sel].ier_elsi = !!(val & 4);
                mregs[sel].ier_edssi = !!(val & 8);
            }
            break;
        case 0x2:
            if (val & 1) {
                mregs[sel].fcr_fifo_enable = true;
                switch ((val & 0xc0) >> 6) {
                    case 0:
                        mregs[sel].fcr_ififo_trigger_level_bytes = 1;
                        break;
                    case 1:
                        mregs[sel].fcr_ififo_trigger_level_bytes = 4;
                        break;
                    case 2:
                        mregs[sel].fcr_ififo_trigger_level_bytes = 8;
                        break;
                    case 3:
                        mregs[sel].fcr_ififo_trigger_level_bytes = 14;
                        break;
                }
            } else {
                mregs[sel].fcr_fifo_enable = false;
                mregs[sel].fcr_ififo_trigger_level_bytes = 1;
            }
            if (val & 2) {
                mregs[sel].ifsz = 0;
            }
            if (val & 4) {
                mregs[sel].ofsz = 0;
            }
            if (mregs[sel].thre_bits_remain == 0) { 
                mregs[sel].thre_intr = true;
            }
            midi_serial_iir_check(sel);
            break;
        case 0x3:
            mregs[sel].lcr_word_length_bits = (val & 0x03)+5;
            mregs[sel].lcr_stb = !!(val & 0x04);
            mregs[sel].lcr_pen = !!(val & 0x08);
            mregs[sel].lcr_eps = !!(val & 0x10);
            mregs[sel].lcr_stick = !!(val & 0x20);
            mregs[sel].lcr_break = !!(val & 0x40);
            mregs[sel].lcr_dlab = !!(val & 0x80);
            break;
        case 0x4:
            mregs[sel].mcr_dtr = !!(val & 0x01);
            mregs[sel].mcr_rts = !!(val & 0x02);
            mregs[sel].mcr_out1 = !!(val & 0x04);
            mregs[sel].mcr_out2 = !!(val & 0x08);
            mregs[sel].mcr_loop = !!(val & 0x10);
            mregs[sel].mcr_afe = !!(val & 0x20);

            if (mregs[sel].mcr_loop) {
                mregs[sel].msr_dcts |= mregs[sel].msr_cts ^ mregs[sel].mcr_dtr;
                mregs[sel].msr_cts = mregs[sel].mcr_dtr;

                mregs[sel].msr_ddsr |= mregs[sel].msr_dsr ^ mregs[sel].mcr_rts;
                mregs[sel].msr_dsr = mregs[sel].mcr_rts;

                if (mregs[sel].msr_ri) {
                    mregs[sel].msr_teri |= mregs[sel].msr_ri ^ mregs[sel].mcr_out1;
                }
                mregs[sel].msr_ri = mregs[sel].mcr_out1;

                mregs[sel].msr_ddcd |= mregs[sel].msr_dcd ^ mregs[sel].mcr_out2;
                mregs[sel].msr_dcd = mregs[sel].mcr_out2;

            } else {
                // DTR tied to RI on opposite UART, wired this way on the official X16 MIDI card
                if (mregs[sel ^ 1].msr_ri && !mregs[sel].mcr_dtr) {
                    mregs[sel ^ 1].msr_teri = true;
                }
                mregs[sel ^ 1].msr_ri = mregs[sel].mcr_dtr;
            }

            break;
        case 0x7:
            mregs[sel].scratch = val;
            break;
    }
}

