/*
 * wav.h
 *
 *  Created on: Sep 1, 2026
 *      Author: Shim Manaloto
 */

#ifndef SRC_WAV_H_
#define SRC_WAV_H_

#include "fatfs.h"

typedef struct
{
    uint32_t sample_rate;
    uint16_t num_channels;
    uint16_t bits_per_sample;
    uint16_t block_align;
    uint32_t data_size;
    uint32_t data_offset;
} wav_header_t;

FRESULT WAV_parse_header(FIL* fil, wav_header_t* header);
FRESULT audio_play(const char* path, I2S_HandleTypeDef* hi2s);
FRESULT audio_pause(void);
FRESULT audio_resume(void);
FRESULT audio_stop(void);
void audio_process();

#endif /* SRC_WAV_H_ */
