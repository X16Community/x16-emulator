#include "ymglue.h"
#include "ymfm_opm.h"
#include <cstdint>

class ym2151_interface : public ymfm::ymfm_interface {
	public:
		ym2151_interface():
			m_chip(*this),
			m_timers{0, 0},
			m_busy_timer{ 0 },
			m_irq_status{ false }
		{ }
		~ym2151_interface() { }

		virtual void ymfm_sync_mode_write(uint8_t data) override {
			m_engine->engine_mode_write(data);
		}

		virtual void ymfm_sync_check_interrupts() override {
			m_engine->engine_check_interrupts();
		}

		virtual void ymfm_set_timer(uint32_t tnum, int32_t duration_in_clocks) override {
			if (tnum >= 2) return;
			m_timers[tnum] = duration_in_clocks;
		}

		virtual void ymfm_set_busy_end(uint32_t clocks) override {
			m_busy_timer = clocks;
		}

		virtual bool ymfm_is_busy() override {
			return m_busy_timer > 0;
		}

		virtual void ymfm_update_irq(bool asserted) override {
			m_irq_status = asserted;
		}

		void update_clocks(int cycles) {
			m_busy_timer = std::max(0, m_busy_timer - (64 * cycles));
			for (int i = 0; i < 2; ++i) {
				if (m_timers[i] > 0) {
					m_timers[i] = std::max(0, m_timers[i] - (64 * cycles));
					if (m_timers[i] <= 0) {
						m_engine->engine_timer_expired(i);
					}
				}
			}	
		}

		void write(uint8_t addr, uint8_t value) {
			if (!ymfm_is_busy()) {
				m_chip.write_address(addr);
				m_chip.write_data(value);
			} else {
				printf("YM2151 write received while busy.\n");
			}
		}

		void generate(int16_t* output, uint32_t numsamples) {
			int s = 0;
			int ls, rs;
			update_clocks(numsamples);
			for (uint32_t i = 0; i < numsamples; i++) {
				m_chip.generate(&opm_out);
				ls = opm_out.data[0];
				rs = opm_out.data[1];
				if (ls < -32768) ls = -32768;
				if (ls > 32767) ls = 32767;
				if (rs < -32768) rs = -32768;
				if (rs > 32767) rs = 32767;
				output[s++] = ls;
				output[s++] = rs;
			}
		}

		uint8_t read_status() {
			return m_chip.read_status();
		}

		bool irq() {
			return m_irq_status;
		}

	private:
		ymfm::ym2151 m_chip;
		int32_t m_timers[2];
		int32_t m_busy_timer;
		bool m_irq_status;

		ymfm::ym2151::output_data opm_out;
};

namespace {
	ym2151_interface opm_iface;
	bool initialized = false;
}

extern "C" {
	void YM_Create(int clock) {
		// clock is fixed at 3.579545MHz
	}

	void YM_init(int sample_rate, int frame_rate) {
		// args are ignored
		initialized = true;
	}

	void YM_stream_update(uint16_t* output, uint32_t numsamples) {
		if (initialized) opm_iface.generate((int16_t*)output, numsamples);
	}

	void YM_write_reg(uint8_t reg, uint8_t val) {
		if (initialized) opm_iface.write(reg, val);
	}

	uint8_t YM_read_status() {
		if (initialized)
			return opm_iface.read_status();
		else
			return 0x00; // prevent programs that wait for the busy flag to clear from locking up (emulator compromise)
	}

	bool YM_irq() {
		if (initialized)
			return opm_iface.irq();
		else
			return false;
	}
}
