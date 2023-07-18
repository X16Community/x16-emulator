#include "ymglue.h"
#include "ymfm_opm.h"
#include <cstdint>

namespace {
    ymfm::ym2151* opm;
    ymfm::ymfm_interface opm_iface;
    ymfm::ym2151::output_data opm_out;
}

extern "C" {
    void YM_Create(int clock) {
        // clock is fixed at 3.579545MHz
    }

    void YM_init(int sample_rate, int frame_rate) {
        // args are ignored
        opm = new ymfm::ym2151(opm_iface);
    }

	void YM_stream_update(uint16_t* output, uint32_t numsamples) {
        int s = 0;
        int ls, rs;
        for (uint32_t i = 0; i < numsamples; i++) {
            opm->generate(&opm_out);
            ls = opm_out.data[0];
            rs = opm_out.data[1];
            if (ls < -32768) ls = -32768;
            if (ls > 32767) ls = 32767;
            if (rs < -32768) rs = -32768;
            if (rs > 32767) rs = 32767;
            output[s++] = (uint16_t)ls;
            output[s++] = (uint16_t)rs;
        }
    }

    void YM_write_reg(uint8_t reg, uint8_t val) {
        opm->write_address(reg);
        opm->write_data(val);
    }

	uint8_t YM_read_status() {
        return opm->read_status();
    }
}
