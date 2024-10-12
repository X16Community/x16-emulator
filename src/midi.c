// Commander X16 Emulator
// Copyright (c) 2024 MooingLemur
// All rights reserved. License: 2-clause BSD

#include <pthread.h>
#include <stdio.h>
#include "glue.h"
#include "midi.h"
#include "audio.h"
#include "endian.h"

#ifdef _WIN32
    #include <windows.h>
    #define LIBRARY_TYPE HMODULE
    #define LOAD_LIBRARY(name) LoadLibrary(name)
    #define GET_FUNCTION(lib, name) (void *)GetProcAddress(lib, name)
    #define CLOSE_LIBRARY(lib) FreeLibrary(lib)
#else
    #include <dlfcn.h>
    #define LIBRARY_TYPE void*
    #define LOAD_LIBRARY(name) dlopen(name, RTLD_LAZY)
    #define GET_FUNCTION(lib, name) dlsym(lib, name)
    #define CLOSE_LIBRARY(lib) dlclose(lib)
#endif

#define ASSIGN_FUNCTION(lib, var, name) {\
    var = GET_FUNCTION(lib, name);\
    if (!var) { fprintf(stderr, "Unable to find symbol for '%s'\n", name); CLOSE_LIBRARY(handle); return; }\
}

enum MIDI_states {
    MSTATE_Normal,
    MSTATE_Param,
    MSTATE_SysEx,
};

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
    uint8_t ibyte_bits_remain;
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

    uint8_t midi_event_fifo[256];
    int mfsz;
    uint8_t in_midi_last_command;

    pthread_mutex_t fifo_mutex;
    pthread_mutexattr_t fifo_mutex_attr;
};

struct midi_serial_regs mregs[2];
static bool serial_midi_mutexes_initialized = false;

void midi_serial_iir_check(uint8_t sel);
bool fs_midi_in_connect = false;

#ifdef HAS_FLUIDSYNTH

int handle_midi_event(void* data, fluid_midi_event_t* event);

static uint8_t sysex_buffer[1024];
static int sysex_bufptr;

static enum MIDI_states midi_state[2] = {MSTATE_Normal, MSTATE_Normal};
static uint8_t out_midi_last_command[2] = {0, 0};
static uint8_t out_midi_first_param[2];
static uint16_t out_midi_nrpn[2][16];
static uint16_t out_midi_rpn[2][16];
static bool out_midi_nrpn_active[2][16];

static bool midi_initialized = false;

static fluid_settings_t* fl_settings;
static fluid_midi_driver_t* fl_mdriver;
static fluid_synth_t* fl_synth;
static int fl_sf2id;

typedef fluid_settings_t* (*new_fluid_settings_f_t)(void);
typedef fluid_synth_t* (*new_fluid_synth_f_t)(fluid_settings_t*);
typedef fluid_audio_driver_t* (*new_fluid_audio_driver_f_t)(fluid_settings_t*, fluid_synth_t*);
typedef fluid_midi_driver_t* (*new_fluid_midi_driver_f_t)(fluid_settings_t*, handle_midi_event_func_t, void*);
typedef int (*fluid_settings_setnum_f_t)(fluid_settings_t*,const char*, double);
typedef int (*fluid_settings_setint_f_t)(fluid_settings_t*,const char*, int);
typedef int (*fluid_settings_setstr_f_t)(fluid_settings_t*,const char*, const char*);
typedef int (*fluid_synth_sfload_f_t)(fluid_synth_t*, const char *, int);
typedef int (*fluid_synth_program_change_f_t)(fluid_synth_t*, int, int);
typedef int (*fluid_synth_channel_pressure_f_t)(fluid_synth_t*, int, int);
typedef int (*fluid_synth_system_reset_f_t)(fluid_synth_t*);
typedef int (*fluid_synth_noteoff_f_t)(fluid_synth_t*, int, int);
typedef int (*fluid_synth_noteon_f_t)(fluid_synth_t*, int, int, int);
typedef int (*fluid_synth_key_pressure_f_t)(fluid_synth_t*, int, int, int);
typedef int (*fluid_synth_cc_f_t)(fluid_synth_t*, int, int, int);
typedef int (*fluid_synth_pitch_bend_f_t)(fluid_synth_t*, int, int);
typedef int (*fluid_synth_sysex_f_t)(fluid_synth_t*, const char*, int, char*, int*, int*, int);
typedef int (*fluid_synth_write_s16_f_t)(fluid_synth_t*, int, void*, int, int, void*, int, int);
typedef int (*fluid_midi_event_get_channel_f_t)(const fluid_midi_event_t*);
typedef int (*fluid_midi_event_get_control_f_t)(const fluid_midi_event_t*);
typedef int (*fluid_midi_event_get_key_f_t)(const fluid_midi_event_t*);
typedef int (*fluid_midi_event_get_lyrics_f_t)(const fluid_midi_event_t*, void**, int*);
typedef int (*fluid_midi_event_get_pitch_f_t)(const fluid_midi_event_t*);
typedef int (*fluid_midi_event_get_program_f_t)(const fluid_midi_event_t*);
typedef int (*fluid_midi_event_get_text_f_t)(const fluid_midi_event_t*, void**, int*);
typedef int (*fluid_midi_event_get_type_f_t)(const fluid_midi_event_t*);
typedef int (*fluid_midi_event_get_value_f_t)(const fluid_midi_event_t*);
typedef int (*fluid_midi_event_get_velocity_f_t)(const fluid_midi_event_t*);
typedef int (*fluid_midi_event_set_type_f_t)(fluid_midi_event_t*, int);

static new_fluid_settings_f_t dl_new_fluid_settings;
static new_fluid_synth_f_t dl_new_fluid_synth;
static new_fluid_audio_driver_f_t dl_new_fluid_audio_driver;
static new_fluid_midi_driver_f_t dl_new_fluid_midi_driver;
static fluid_settings_setnum_f_t dl_fluid_settings_setnum;
static fluid_settings_setint_f_t dl_fluid_settings_setint;
static fluid_settings_setstr_f_t dl_fluid_settings_setstr;
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
static fluid_synth_write_s16_f_t dl_fs_write_s16;
static fluid_midi_event_get_channel_f_t dl_fluid_midi_event_get_channel;
static fluid_midi_event_get_control_f_t dl_fluid_midi_event_get_control;
static fluid_midi_event_get_key_f_t dl_fluid_midi_event_get_key;
static fluid_midi_event_get_lyrics_f_t dl_fluid_midi_event_get_lyrics;
static fluid_midi_event_get_pitch_f_t dl_fluid_midi_event_get_pitch;
static fluid_midi_event_get_program_f_t dl_fluid_midi_event_get_program;
static fluid_midi_event_get_text_f_t dl_fluid_midi_event_get_text;
static fluid_midi_event_get_type_f_t dl_fluid_midi_event_get_type;
static fluid_midi_event_get_value_f_t dl_fluid_midi_event_get_value;
static fluid_midi_event_get_velocity_f_t dl_fluid_midi_event_get_velocity;
static fluid_midi_event_set_type_f_t dl_fluid_midi_event_set_type;


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
    LIBRARY_TYPE handle = LOAD_LIBRARY("libfluidsynth.so.3");
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
    ASSIGN_FUNCTION(handle, dl_new_fluid_midi_driver, "new_fluid_midi_driver");
    ASSIGN_FUNCTION(handle, dl_fluid_settings_setnum, "fluid_settings_setnum");
    ASSIGN_FUNCTION(handle, dl_fluid_settings_setint, "fluid_settings_setint");
    ASSIGN_FUNCTION(handle, dl_fluid_settings_setstr, "fluid_settings_setstr");
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
    ASSIGN_FUNCTION(handle, dl_fs_write_s16, "fluid_synth_write_s16");
    ASSIGN_FUNCTION(handle, dl_fluid_midi_event_get_channel, "fluid_midi_event_get_channel");
    ASSIGN_FUNCTION(handle, dl_fluid_midi_event_get_control, "fluid_midi_event_get_control");
    ASSIGN_FUNCTION(handle, dl_fluid_midi_event_get_key, "fluid_midi_event_get_key");
    ASSIGN_FUNCTION(handle, dl_fluid_midi_event_get_lyrics, "fluid_midi_event_get_lyrics");
    ASSIGN_FUNCTION(handle, dl_fluid_midi_event_get_pitch, "fluid_midi_event_get_pitch");
    ASSIGN_FUNCTION(handle, dl_fluid_midi_event_get_program, "fluid_midi_event_get_program");
    ASSIGN_FUNCTION(handle, dl_fluid_midi_event_get_text, "fluid_midi_event_get_text");
    ASSIGN_FUNCTION(handle, dl_fluid_midi_event_get_type, "fluid_midi_event_get_type");
    ASSIGN_FUNCTION(handle, dl_fluid_midi_event_get_value, "fluid_midi_event_get_value");
    ASSIGN_FUNCTION(handle, dl_fluid_midi_event_get_velocity, "fluid_midi_event_get_velocity");
    ASSIGN_FUNCTION(handle, dl_fluid_midi_event_set_type, "fluid_midi_event_set_type");

    fl_settings = dl_new_fluid_settings();
    dl_fluid_settings_setnum(fl_settings, "synth.sample-rate", 
    AUDIO_SAMPLERATE);
    dl_fluid_settings_setnum(fl_settings, "synth.gain", FL_DEFAULT_GAIN);
    dl_fluid_settings_setstr(fl_settings, "midi.portname", "Commander X16 Emulator");
    dl_fluid_settings_setint(fl_settings, "midi.autoconnect", fs_midi_in_connect);
    fl_synth = dl_new_fluid_synth(fl_settings);
    if (fs_midi_in_connect) {
        fl_mdriver = dl_new_fluid_midi_driver(fl_settings, handle_midi_event, &mregs[0]);
    }

    midi_initialized = true;
    fprintf(stderr, "Initialized MIDI synth at $%04X.\n", midi_card_addr);
}

void midi_load_sf2(uint8_t* filename)
{
    if (!midi_initialized) return;
    fl_sf2id = dl_fs_sfload(fl_synth, (const char *)filename, true);
    if (fl_sf2id == FLUID_FAILED) {
        fprintf(stderr, "Unable to load soundfont.\n");
    }
}

// Receive a byte from client
// Store state, or dispatch event
void midi_byte_out(uint8_t sel, uint8_t b)
{
    uint8_t chan = out_midi_last_command[sel] & 0xf;

    if (!midi_initialized) return;
    switch (midi_state[sel]) {
        case MSTATE_Normal:
            if (b < 0x80) {
                if ((out_midi_last_command[sel] & 0xf0) == 0xc0) { // patch change
                    dl_fs_program_change(fl_synth, out_midi_last_command[sel] & 0xf, b);
                } else if ((out_midi_last_command[sel] & 0xf0) == 0xd0) { // channel pressure
                    dl_fs_channel_pressure(fl_synth, out_midi_last_command[sel] & 0xf, b);
                } else if (out_midi_last_command[sel] >= 0x80) { // two-param command
                    out_midi_first_param[sel] = b;
                    midi_state[sel] = MSTATE_Param;
                }
            } else {
                if (b < 0xf0) {
                    out_midi_last_command[sel] = b;
                } else if (b == 0xf0) {
                    sysex_bufptr = 0;
                    midi_state[sel] = MSTATE_SysEx;
                    out_midi_last_command[sel] = 0;
                } else if (b == 0xff) {
                    dl_fs_system_reset(fl_synth);
                    out_midi_last_command[sel] = 0;
                } else if (b < 0xf8) {
                    out_midi_last_command[sel] = 0;
                }
            }
            break;
        case MSTATE_Param:
            switch (out_midi_last_command[sel] & 0xf0) {
                case 0x80: // note off
                    dl_fs_noteoff(fl_synth, chan, out_midi_first_param[sel]); // no release velocity
                    break;
                case 0x90: // note on
                    if (b == 0) {
                        dl_fs_noteoff(fl_synth, chan, out_midi_first_param[sel]);
                    } else {
                        dl_fs_noteon(fl_synth, chan, out_midi_first_param[sel], b);
                    }
                    break;
                case 0xa0: // aftertouch
                    dl_fs_key_pressure(fl_synth, chan, out_midi_first_param[sel], b);
                    break;
                case 0xb0: // controller
                    switch (out_midi_first_param[sel]) {
                        case 98:
                            out_midi_nrpn_active[sel][chan] = true;
                            out_midi_nrpn[sel][chan] = (out_midi_nrpn[sel][chan] & 0x00ff) | (b << 8);
                            break;
                        case 99:
                            out_midi_nrpn_active[sel][chan] = true;
                            out_midi_nrpn[sel][chan] = (out_midi_nrpn[sel][chan] & 0xff00) | b;
                            break;
                        case 100:
                            out_midi_nrpn_active[sel][chan] = false;
                            out_midi_rpn[sel][chan] = (out_midi_rpn[sel][chan] & 0x00ff) | (b << 8);
                            break;
                        case 101:
                            out_midi_nrpn_active[sel][chan] = false;
                            out_midi_rpn[sel][chan] = (out_midi_rpn[sel][chan] & 0xff00) | b;
                            break;
                    }
                    dl_fs_cc(fl_synth, chan, out_midi_first_param[sel], b);
                    if (out_midi_nrpn_active[sel][chan] && out_midi_nrpn[sel][chan] == 0x0121 && out_midi_first_param[sel] == 6) {
                        dl_fs_cc(fl_synth, chan, 71, b); // Translate NRPN 0x0121 -> Controller 71 (Timbre/Resonance)
                    }
                    break;
                case 0xe0: // pitch bend
                    dl_fs_pitch_bend(fl_synth, chan, ((uint16_t)out_midi_first_param[sel]) | (uint16_t)b << 7);
                    break;
            }
            midi_state[sel] = MSTATE_Normal;
            break;
        case MSTATE_SysEx:
            if (b & 0x80) { // any command byte can terminate a SYSEX, not just 0xf7
                if (sysex_bufptr < (sizeof(sysex_buffer) / sizeof(sysex_buffer[0]))-1) { // only if buffer didn't fill
                    sysex_buffer[sysex_bufptr] = 0;
                    if (!strncmp((char *)sysex_buffer, "\x7F\x7F\x04\x01\x00", sysex_bufptr-1)) {
                        // Handle Master Volume SysEx
                        dl_fluid_settings_setnum(fl_settings, "synth.gain", FL_DEFAULT_GAIN*((float)sysex_buffer[sysex_bufptr-1]/127));
                    } else if (!strncmp((char *)sysex_buffer, "\x7E\x7F\x09\x01", sysex_bufptr)) {
                        // Handle General MIDI reset SysEx
                        dl_fs_system_reset(fl_synth);
                    } else {
                        dl_fs_sysex(fl_synth, (char *)sysex_buffer, sysex_bufptr, NULL, NULL, NULL, 0);
                    }
                }
                midi_state[sel] = MSTATE_Normal;
                return midi_byte_out(sel, b);
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

void midi_synth_render(int16_t* buf, int len)
{
    if (midi_initialized) {
        dl_fs_write_s16(fl_synth, len, buf, 0, 2, buf, 1, 2);
    } else {
        memset(buf, 0, len * 2 * sizeof(int16_t));
    }
}

void midi_event_enqueue_byte(struct midi_serial_regs* mrp, uint8_t val)
{
    if (mrp->mfsz >= 256) return;
    mrp->midi_event_fifo[mrp->mfsz++] = val;
}

void midi_event_enqueue_short(struct midi_serial_regs* mrp, uint8_t cmd, uint8_t val)
{
    if (mrp->mfsz >= 255) {
        mrp->in_midi_last_command = 0;
        return;
    }
    if (mrp->in_midi_last_command != cmd) {
        mrp->midi_event_fifo[mrp->mfsz++] = cmd;
        mrp->in_midi_last_command = cmd;
    }
    mrp->midi_event_fifo[mrp->mfsz++] = val;
}

void midi_event_enqueue_normal(struct midi_serial_regs* mrp, uint8_t cmd, uint8_t key, uint8_t val)
{
    if (mrp->mfsz >= 254) {
        mrp->in_midi_last_command = 0;
        return;
    }
    if (mrp->in_midi_last_command != cmd) {
        mrp->midi_event_fifo[mrp->mfsz++] = cmd;
        mrp->in_midi_last_command = cmd;
    }
    mrp->midi_event_fifo[mrp->mfsz++] = key;
    mrp->midi_event_fifo[mrp->mfsz++] = val;
}

void midi_event_enqueue_sysex(struct midi_serial_regs* mrp, uint8_t *bufptr, int buflen)
{
    int i;

    mrp->in_midi_last_command = 0;
    if (mrp->mfsz >= 255L - (buflen + 2)) { // too full
        return;
    }

    mrp->midi_event_fifo[mrp->mfsz++] = 0xf0;
    for (i = 0; i < buflen; i++) {
        mrp->midi_event_fifo[mrp->mfsz++] = bufptr[i];
    }
    mrp->midi_event_fifo[mrp->mfsz++] = 0xf7;
}

int handle_midi_event(void* data, fluid_midi_event_t* event)
{
    struct midi_serial_regs* mrp = (struct midi_serial_regs*)data;
    pthread_mutex_lock(&mrp->fifo_mutex);

    uint8_t type = dl_fluid_midi_event_get_type(event);
    uint8_t chan = dl_fluid_midi_event_get_channel(event);
    uint8_t cmd = (type < 0x80 || type >= 0xf0) ? type : (type | (chan & 0xf));
    uint8_t key, val;
    uint8_t *bufptr;
    int buflen;

    switch (type) {
        case FS_NOTE_OFF:
        case FS_NOTE_ON:
            key = dl_fluid_midi_event_get_key(event);
            val = dl_fluid_midi_event_get_velocity(event);
            midi_event_enqueue_normal(mrp, cmd, key, val);
            break;
        case FS_KEY_PRESSURE:
            key = dl_fluid_midi_event_get_key(event);
            val = dl_fluid_midi_event_get_value(event);
            midi_event_enqueue_normal(mrp, cmd, key, val);
            break;
        case FS_CONTROL_CHANGE:
            key = dl_fluid_midi_event_get_control(event);
            val = dl_fluid_midi_event_get_value(event);
            midi_event_enqueue_normal(mrp, cmd, key, val);
            break;
        case FS_PITCH_BEND:
            key = dl_fluid_midi_event_get_pitch(event) & 0x7f;
            val = (dl_fluid_midi_event_get_pitch(event) >> 7) & 0x7f;
            midi_event_enqueue_normal(mrp, cmd, key, val);
            break;
        case FS_PROGRAM_CHANGE:
        case FS_CHANNEL_PRESSURE:
            val = dl_fluid_midi_event_get_program(event);
            midi_event_enqueue_short(mrp, cmd, val);
            break;
        case FS_MIDI_TIME_CODE:
            val = dl_fluid_midi_event_get_value(event);
            midi_event_enqueue_short(mrp, type, val);
            mrp->in_midi_last_command = 0;
            break;
        case FS_MIDI_TUNE_REQUEST:
        case 0xF4:
        case 0xF5:
            midi_event_enqueue_byte(mrp, type);
            mrp->in_midi_last_command = 0;
            break;
        case FS_MIDI_SYNC:
        case FS_MIDI_TICK:
        case FS_MIDI_START:
        case FS_MIDI_CONTINUE:
        case FS_MIDI_STOP:
        case FS_MIDI_ACTIVE_SENSING:
        case FS_MIDI_SYSTEM_RESET:
            midi_event_enqueue_byte(mrp, type);
            break;
        case FS_MIDI_SYSEX:
            // FluidSynth doesn't offer a get_sysex function, but internally
            // a text event is equivalent to a sysex event, in terms of what
            // parts of the event structure are populated
            //
            // Unfortunately that means we have to fool it in order to
            // access the data.
            dl_fluid_midi_event_set_type(event, FS_MIDI_TEXT);
            if (dl_fluid_midi_event_get_text(event, (void **)&bufptr, &buflen) == FLUID_OK && bufptr != NULL) {
                midi_event_enqueue_sysex(mrp, bufptr, buflen);
            }
            break;
    }

    //fprintf(stderr, "Debug: MIDI IN: Type: %02X Chan: %02X\n", type, chan);

    pthread_mutex_unlock(&mrp->fifo_mutex);
    return FLUID_OK;
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

void midi_byte_out(uint8_t sel, uint8_t b)
{
    // no-op
}

void midi_synth_render(int16_t* buf, int len)
{
    // no synth, return zeroed buffer
    memset(buf, 0, len * 2 * sizeof(int16_t));
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

        mregs[sel].ibyte_bits_remain = 7;

        mregs[sel].ifsz = 0;
        mregs[sel].ififo[0] = 0;
        mregs[sel].ofsz = 0;
        mregs[sel].clock = 0;
        mregs[sel].clockdec = 0;
        mregs[sel].last_warning = 0;

        mregs[sel].mfsz = 0;
        mregs[sel].in_midi_last_command = 0;
    }

    if (!serial_midi_mutexes_initialized) {
        for (sel=0; sel<2; sel++) {
            pthread_mutexattr_init(&mregs[sel].fifo_mutex_attr);
            pthread_mutex_init(&mregs[sel].fifo_mutex, &mregs[sel].fifo_mutex_attr);
        }
        serial_midi_mutexes_initialized = true;
    }
}

void midi_serial_iir_check(uint8_t sel)
{
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
                    midi_byte_out(sel, mregs[sel].ofifo[0]);
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

            if (mregs[sel].rx_timeout > 0) {
                mregs[sel].rx_timeout--;
            }

            if (mregs[sel].ibyte_bits_remain > 0 && mregs[sel].mfsz > 0) {
                mregs[sel].ibyte_bits_remain--;
            }
            if (mregs[sel].ibyte_bits_remain == 0 && mregs[sel].mfsz > 0) {
                if (mregs[sel].ifsz < (mregs[sel].fcr_fifo_enable ? 16 : 1)) {
                    mregs[sel].ififo[mregs[sel].ifsz++] = mregs[sel].midi_event_fifo[0];
                    mregs[sel].rx_timeout_enabled = true;
                    mregs[sel].rx_timeout = 4 * (2 + mregs[sel].lcr_word_length_bits + mregs[sel].lcr_stb + mregs[sel].lcr_pen);
                } else {
                    mregs[sel].lsr_oe = true; // inbound FIFO overflow
                    if (!mregs[sel].fcr_fifo_enable) {
                        // RBR is overwritten by RSR in this mode
                        // whenever overflow happens
                        mregs[sel].ififo[mregs[sel].ifsz-1] = mregs[sel].midi_event_fifo[0];
                    }
                }
                mregs[sel].mfsz--;
                if (mregs[sel].mfsz > 0) {
                    for (i=0; i<mregs[sel].mfsz; i++) {
                        mregs[sel].midi_event_fifo[i] = mregs[sel].midi_event_fifo[i+1];
                    }
                }
                mregs[sel].ibyte_bits_remain = 2 + mregs[sel].lcr_word_length_bits + mregs[sel].lcr_stb + mregs[sel].lcr_pen;
            }

            pthread_mutex_unlock(&mregs[sel].fifo_mutex);
            mregs[sel].clock += 0x1000000LL;
            midi_serial_iir_check(sel);
        }
    }
}

uint8_t midi_serial_dequeue_ibyte(uint8_t sel)
{
    pthread_mutex_lock(&mregs[sel].fifo_mutex);
    uint8_t ret = mregs[sel].ififo[0];
    uint8_t i;

    if (mregs[sel].ifsz > 0) {
        mregs[sel].ifsz--;
        if (mregs[sel].ifsz == 0) {
            mregs[sel].rx_timeout_enabled = false;
        } else {
            for (i=0; i<mregs[sel].ifsz; i++) {
                mregs[sel].ififo[i] = mregs[sel].ififo[i+1];
            }
            mregs[sel].rx_timeout_enabled = true;
            mregs[sel].rx_timeout = 4 * (2 + mregs[sel].lcr_word_length_bits + mregs[sel].lcr_stb + mregs[sel].lcr_pen);
        }
        midi_serial_iir_check(sel);
    }

    pthread_mutex_unlock(&mregs[sel].fifo_mutex);
    return ret;
}

void midi_serial_enqueue_obyte(uint8_t sel, uint8_t val)
{
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
        fprintf(stderr, "Serial MIDI: Warning: UART %d TX Overflow\n", sel);
    }
    midi_serial_iir_check(sel);
    pthread_mutex_unlock(&mregs[sel].fifo_mutex);
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
                if (debugOn) {
                    return mregs[sel].ififo[0];
                } else {
                    return midi_serial_dequeue_ibyte(sel);
                }
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
    time_t now = time(NULL);

    //printf("midi_serial_write %d %d\n", reg, val);
    uint8_t sel = (reg & 8) >> 3;
    switch (reg & 7) {
        case 0x0:
            if (mregs[sel].lcr_dlab) {
                mregs[sel].dll = val;
                midi_serial_calculate_clk(sel);
            } else {
                if (mregs[sel].lcr_word_length_bits != 8 || mregs[sel].lcr_stb || mregs[sel].lcr_pen) {
                    if (mregs[sel].last_warning + 60 < now) {
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
                        mregs[sel].last_warning = now;
                    }
                } else if (mregs[sel].lcr_break && mregs[sel].last_warning + 60 < now) {
                    fprintf(stderr, "Serial MIDI: Warning: break improperly set for UART %d.\n", sel);
                    mregs[sel].last_warning = now;
                } else if (mregs[sel].dl != 32 && mregs[sel].last_warning + 60 < now) {
                    fprintf(stderr, "Serial MIDI: Warning: improper divisor %d for UART %d, must be set to 32 for standard MIDI bitrate.\n", mregs[sel].dl, sel);
                    mregs[sel].last_warning = now;
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
            if (mregs[sel].mfsz == 0) {
                mregs[sel].ibyte_bits_remain = 2 + mregs[sel].lcr_word_length_bits + mregs[sel].lcr_stb + mregs[sel].lcr_pen;
            }
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

bool midi_serial_irq(void)
{
    bool uart0int = (mregs[0].iir & 1) == 0 && mregs[0].mcr_out2;
    bool uart1int = (mregs[1].iir & 1) == 0 && mregs[1].mcr_out2;
    return (uart0int | uart1int);
}
