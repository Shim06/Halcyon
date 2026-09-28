/*
 * audio_metadata.h
 *
 *  Created on: Sep 4, 2026
 *      Author: Shim Manaloto
 */

#ifndef INC_AUDIO_METADATA_H_
#define INC_AUDIO_METADATA_H_

#include "fatfs.h"
#include <stdint.h>
#include <string.h>

#define AUDIO_METADATA_TAG_LEN 256U

typedef struct
{
    char artist[AUDIO_METADATA_TAG_LEN];
    char album[AUDIO_METADATA_TAG_LEN];
    char genre[AUDIO_METADATA_TAG_LEN];
} audio_metadata_t;

FRESULT read_audio_metadata(const char* path, audio_metadata_t* metadata)
{
    FIL fp;
    FRESULT fr;
//    UINT br;

    if (path == NULL || metadata == NULL) return FR_INVALID_PARAMETER;
    memset(metadata, 0, sizeof(*metadata));

    fr = f_open(&fp, path, FA_READ);
    if (fr != FR_OK) return fr;

    // Determine audio file extension
    const char* ext = strrchr(path, '.');
    if (ext == NULL)
    {
        f_close(&fp);
        return FR_INVALID_NAME;
    }

    if (strcasecmp(ext, ".flac") == 0)
    {
        /*
         * FLAC metadata parsing goes here.
         */
    }
    else if (strcasecmp(ext, ".wav") == 0)
    {
        const char* filename;
        const char* separator;
        size_t artist_len;

        filename = strrchr(path, '/');

        if (filename != NULL) filename++;
        else filename = path;

        separator = strstr(filename, " - ");
        if (separator != NULL)
        {
            artist_len = (size_t)(separator - filename);

            if (artist_len >= AUDIO_METADATA_TAG_LEN) artist_len = AUDIO_METADATA_TAG_LEN - 1U;

            memcpy(metadata->artist, filename, artist_len);
            metadata->artist[artist_len] = '\0';
        }
    }

    else if (strcasecmp(ext, ".mp3") == 0)
    {
        /*
         * ID3 metadata parsing goes here.
         */
    }
    else
    {
        f_close(&fp);
        return FR_INVALID_NAME;
    }

    fr = f_close(&fp);

    if (fr != FR_OK) return fr;

    return FR_OK;
}

#endif /* INC_AUDIO_METADATA_H_ */
