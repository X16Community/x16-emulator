#ifndef YMGLUE_H
#define YMGLUE_H

#ifdef __cplusplus
extern "C" {
#endif
	#include <stdint.h>

	uint8_t YM_read_status(void);
	void YM_Create(int clock);
	void YM_init(int sample_rate, int frame_rate);
	void YM_stream_update(uint16_t* output, uint32_t numsamples);
	void YM_write_reg(uint8_t reg, uint8_t val);
	bool YM_irq(void);

#ifdef __cplusplus
}
#endif

#endif
