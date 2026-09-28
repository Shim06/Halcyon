/*
 * track_indexer.h
 *
 *  Created on: Sep 3, 2026
 *      Author: Shim Manaloto
 */

#ifndef INC_TRACK_INDEXER_H_
#define INC_TRACK_INDEXER_H_

#include "fatfs.h"
#include <stdint.h>

#define INDEX_MAGIC 0x58444948U // <- 'HIDX'
#define INDEX_VERSION 1U

#define INDEX_DIR            ".Halcyon"
#define INDEX_BUILD_DIR      ".Halcyon/.build"

#define MAX_PATH_LEN 512U
#define MAX_TAG_LEN 256U

typedef enum
{
    TIDX_TRACK_WAV = 0, TIDX_TRACK_FLAC, TIDX_TRACK_MP3,
} track_fmt_t;

typedef enum
{
    TIDX_GROUP_BY_FILE = 0, TIDX_GROUP_BY_ARTIST, TIDX_GROUP_BY_ALBUM, TIDX_GROUP_BY_GENRE,
} group_mode_t;

typedef enum
{
    TIDX_INDEX_VALID = 0, TIDX_STALE, TIDX_MISSING,
} index_status_t;

typedef struct __attribute__((packed))
{
    uint32_t magic;
    uint32_t version;
    uint32_t entry_count;
    uint16_t dir_fdate;
    uint16_t dir_ftime;
    uint8_t reserved[4];
} index_header_t;

typedef struct
{
    char filepath[MAX_PATH_LEN];
} index_entry_t;

typedef struct __attribute__((packed))
{
    char filepath[MAX_PATH_LEN];
    char artist[MAX_TAG_LEN];
    char album[MAX_TAG_LEN];
    char genre[MAX_TAG_LEN];
} build_entry_t;

typedef struct
{
    FIL fp;
    index_header_t header;
    uint32_t current;
    uint8_t global;
} index_cursor_t;

FRESULT index_build(void);
FRESULT index_validate(void);
FRESULT index_open(index_cursor_t* cursor, const char* path);
FRESULT index_close(index_cursor_t* cursor);
FRESULT index_read(index_cursor_t* cursor, uint32_t index, index_entry_t* entry);

#endif /* INC_TRACK_INDEXER_H_ */
