#ifndef INC_NAVIGATION_H_
#define INC_NAVIGATION_H_

#include "fatfs.h"
#include "track_indexer.h"
#include <stdint.h>

#define NAVIGATION_PATH ".Halcyon/navigation.idx"

#define NAVIGATION_MAGIC   0x56414E48U /* 'HNAV' */
#define NAVIGATION_VERSION 1U

typedef struct __attribute__((packed))
{
    uint32_t magic;
    uint32_t version;

    uint32_t global_file_offset;

    uint32_t global_artist_count;
    uint32_t global_artist_table_offset;

    uint32_t global_album_count;
    uint32_t global_album_table_offset;

    uint32_t global_genre_count;
    uint32_t global_genre_table_offset;

    uint32_t folder_file_count;
    uint32_t folder_file_table_offset;

    uint32_t folder_artist_count;
    uint32_t folder_artist_table_offset;

    uint32_t folder_album_count;
    uint32_t folder_album_table_offset;

    uint32_t folder_genre_count;
    uint32_t folder_genre_table_offset;

    uint8_t reserved[4];
} navigation_index_header_t;

typedef struct __attribute__((packed))
{
    uint8_t mode;
    uint8_t reserved[3];

    uint32_t entry_count;
    uint32_t entry_table_offset;
} navigation_folder_t;

FRESULT navigation_build(void);
FRESULT navigation_read(const group_mode_t mode, const uint32_t folder_index,
        const uint32_t mode_entry_index, index_entry_t* entry, uint32_t* folder_count,
        uint32_t* group_count);

#endif /* INC_NAVIGATION_H_ */
