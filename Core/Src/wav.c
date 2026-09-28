/*
 * wav.c
 *
 *  Created on: Sep 1, 2026
 *      Author: Shim Manaloto
 */

#include <string.h>
#include <stdio.h>
#include "wav.h"

extern SD_HandleTypeDef hsd;
extern DMA_HandleTypeDef hdma_sdio;

#define AUDIO_HALF_BUF_SIZE (8192 * 2)
#define AUDIO_BUF_SIZE (AUDIO_HALF_BUF_SIZE * 2)
static uint16_t audio_buf[AUDIO_BUF_SIZE] __attribute__((aligned(32)));

static FIL fil;
static wav_header_t header;
static I2S_HandleTypeDef* audio_hi2s = NULL;

static uint8_t audio_active = 0;
static uint8_t audio_paused = 0;

static uint32_t bytes_remaining;
static volatile uint8_t refill_half = 0;
static volatile uint8_t refill_full = 0;
static volatile uint8_t playback_finished = 0;
static volatile uint8_t playback_error = 0;

#define VOLUME_PERCENT 8

static void refill_buffer(uint16_t* buffer);
static void apply_volume(uint16_t* buffer, UINT sample_count);

FRESULT audio_play(const char* path, I2S_HandleTypeDef* hi2s)
{
    FRESULT fr;
    HAL_StatusTypeDef status;

    if (audio_active) audio_stop();
    audio_hi2s = hi2s;
    refill_half = 0;
    refill_full = 0;
    playback_finished = 0;
    playback_error = 0;
    audio_paused = 0;

    fr = f_open(&fil, path, FA_READ);
    if (fr != FR_OK)
    {
        printf("f_open failed: (%i)\r\n", fr);
        return fr;
    }

    fr = WAV_parse_header(&fil, &header);
    if (fr != FR_OK)
    {
        f_close(&fil);
        return fr;
    }

    printf("WAV: %lu Hz, %u ch, %u bit, blockAlign=%u, dataSize=%lu bytes\r\n", header.sample_rate,
            header.num_channels, header.bits_per_sample, header.block_align, header.data_size);

    fr = f_lseek(&fil, header.data_offset);
    if (fr != FR_OK)
    {
        f_close(&fil);
        return fr;
    }

    bytes_remaining = header.data_size;

    memset(audio_buf, 0, sizeof(audio_buf));
    refill_buffer(&audio_buf[0]);
    refill_buffer(&audio_buf[AUDIO_HALF_BUF_SIZE]);

    status = HAL_I2S_Transmit_DMA(hi2s, audio_buf, AUDIO_BUF_SIZE);
    if (status != HAL_OK)
    {
        printf("HAL_I2S_Transmit_DMA failed: %d\r\n", status);
        f_close(&fil);
        return FR_INT_ERR;
    }

    audio_active = 1;
    printf("DMA playback started\r\n");

    return FR_OK;
}

FRESULT audio_pause(void)
{
    if (!audio_active) return FR_INVALID_OBJECT;
    if (audio_paused) return FR_OK;
    if (HAL_I2S_DMAPause(audio_hi2s) != HAL_OK) return FR_INT_ERR;
    audio_paused = 1;

    return FR_OK;
}

FRESULT audio_resume(void)
{
    if (!audio_active) return FR_INVALID_OBJECT;
    if (!audio_paused) return FR_OK;
    if (HAL_I2S_DMAResume(audio_hi2s) != HAL_OK) return FR_INT_ERR;
    audio_paused = 0;

    return FR_OK;
}

FRESULT audio_stop(void)
{
    FRESULT fr;
    if (!audio_active) return FR_OK;
    if (HAL_I2S_DMAStop(audio_hi2s) != HAL_OK) return FR_INT_ERR;
    fr = f_close(&fil);
    audio_active = 0;
    audio_paused = 0;

    return fr;
}

void audio_process()
{
    if (refill_half)
    {
        refill_half = 0;
        refill_buffer(&audio_buf[0]);
    }

    else if (refill_full)
    {
        refill_full = 0;
        refill_buffer(&audio_buf[AUDIO_HALF_BUF_SIZE]);
    }

    if (bytes_remaining == 0)
    {
        if (refill_half && refill_full)
        {
            HAL_I2S_DMAStop(audio_hi2s);
            f_close(&fil);
            playback_finished = 1;
        }
    }

}

static void refill_buffer(uint16_t* buffer)
{
    FRESULT fr;
    UINT br;

    if (bytes_remaining == 0)
    {
        printf("EOF: zero filling buffer\r\n");
        memset(buffer, 0, AUDIO_HALF_BUF_SIZE * sizeof(uint16_t));
        return;
    }

    UINT bytes_to_read =
            (bytes_remaining < AUDIO_HALF_BUF_SIZE * sizeof(uint16_t)) ?
                    bytes_remaining : AUDIO_HALF_BUF_SIZE * sizeof(uint16_t);

    fr = f_read(&fil, buffer, bytes_to_read, &br);
    if (fr != FR_OK)
    {
        printf("f_read failed: %d\r\n", fr);
        memset(buffer, 0, AUDIO_HALF_BUF_SIZE * sizeof(uint16_t));
        playback_error = 1;
        bytes_remaining = 0;
        return;
    }
    bytes_remaining -= br;

    if (br < AUDIO_HALF_BUF_SIZE * sizeof(uint16_t))
    {
        memset((uint8_t*)buffer + br, 0,
        AUDIO_HALF_BUF_SIZE * sizeof(uint16_t) - br);
        bytes_remaining = 0;
    }

    apply_volume(buffer, br / sizeof(uint16_t));
}

static void apply_volume(uint16_t* buffer, UINT sample_count)
{
#if VOLUME_PERCENT != 100

    int16_t* samples = (int16_t*)buffer;
    for (UINT i = 0; i < sample_count; i++)
        samples[i] = (int16_t)(((int32_t)samples[i] * VOLUME_PERCENT) / 100);

#else
    (void)buffer;
    (void)sample_count;
#endif
}

FRESULT WAV_parse_header(FIL* fil, wav_header_t* header)
{
    uint8_t chunk_id[4];
    uint32_t chunk_size;
    UINT br;
    FRESULT fr;
    uint8_t has_fmt = 0;
    uint8_t has_data = 0;

    // Parse riff header
    uint8_t riff_header[12];
    fr = f_read(fil, riff_header, 12, &br);
    if (fr != FR_OK || br != 12) return FR_INT_ERR;

    if (memcmp(&riff_header[0], "RIFF", 4) != 0 || memcmp(&riff_header[8], "WAVE", 4) != 0)
    {
        printf("Not a valid WAV file\r\n");
        return FR_INT_ERR;
    }

    // Walk the header chunks
    while (1)
    {
        fr = f_read(fil, chunk_id, 4, &br);
        if (fr != FR_OK || br != 4) break;

        fr = f_read(fil, &chunk_size, 4, &br);
        if (fr != FR_OK || br != 4) return FR_INT_ERR;

        if (memcmp(chunk_id, "fmt ", 4) == 0)
        {
            static const int fmt_chunk_size = 16;
            uint8_t fmt_buf[fmt_chunk_size];
            if (chunk_size < fmt_chunk_size) return FR_INT_ERR;

            fr = f_read(fil, fmt_buf, 16, &br);
            if (fr != FR_OK || br != 16) return FR_INT_ERR;

            uint16_t audio_format = fmt_buf[0] | (fmt_buf[1] << 8);
            header->num_channels = fmt_buf[2] | (fmt_buf[3] << 8);
            header->sample_rate = fmt_buf[4] | (fmt_buf[5] << 8) | (fmt_buf[6] << 16)
                    | (fmt_buf[7] << 24);
            header->block_align = fmt_buf[12] | (fmt_buf[13] << 8);
            header->bits_per_sample = fmt_buf[14] | (fmt_buf[15] << 8);

            if (audio_format != 1 && audio_format != 0xFFFE)
            {
                printf("Unsupported WAV format tag: %u\r\n", audio_format);
                return FR_INT_ERR;
            }

            if (chunk_size > 16) f_lseek(fil, f_tell(fil) + (chunk_size - 16));
            has_fmt = 1;
        }
        else if (memcmp(chunk_id, "data", 4) == 0)
        {
            header->data_offset = f_tell(fil);
            header->data_size = chunk_size;
            has_data = 1;
        }
        else f_lseek(fil, f_tell(fil) + chunk_size);

        if (memcmp(chunk_id, "data", 4) != 0 && (chunk_size & 1)) f_lseek(fil, f_tell(fil) + 1);

        if (has_fmt && has_data) break;
    }

    return FR_OK;
}

void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef* hi2s)
{
    refill_half = 1;
}

void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef* hi2s)
{
    refill_full = 1;
}
