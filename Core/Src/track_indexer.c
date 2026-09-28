/*
 * track_indexer.c
 *
 *  Created on: Sep 3, 2026
 *      Author: Shim Manaloto
 */

#include "track_indexer.h"
#include "audio_metadata.h"
#include "navigation.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern FATFS fatfs;

static int is_audio_file(const char* filename)
{
    const char* ext = strrchr(filename, '.');
    if (!ext) return 0;

    if (strcasecmp(ext, ".wav") == 0) return 1;
    if (strcasecmp(ext, ".flac") == 0) return 1;
    if (strcasecmp(ext, ".mp3") == 0) return 1;

    return 0;
}

static const char* get_group_name(const build_entry_t* entry, const group_mode_t mode)
{
    switch (mode)
    {
    case TIDX_GROUP_BY_ARTIST:
        return entry->artist;
    case TIDX_GROUP_BY_ALBUM:
        return entry->album;
    case TIDX_GROUP_BY_GENRE:
        return entry->genre;
    default:
        return NULL;
    }
}

static const char* get_mode_name(group_mode_t mode)
{
    switch (mode)
    {
    case TIDX_GROUP_BY_FILE:
        return "file";
        break;
    case TIDX_GROUP_BY_ARTIST:
        return "artist";
        break;
    case TIDX_GROUP_BY_ALBUM:
        return "album";
        break;
    case TIDX_GROUP_BY_GENRE:
        return "genre";
        break;
    default:
        return NULL;
    }
}

static FRESULT get_folder_hash(const char* temp_path, uint64_t* folder_hash)
{
    const char* filename;
    char* end;

    filename = strrchr(temp_path, '/');
    if (filename == NULL) return FR_INVALID_NAME;

    filename++;

    *folder_hash = strtoull(filename, &end, 16);

    if (end == filename || strcmp(end, ".tmp") != 0) return FR_INVALID_NAME;

    return FR_OK;
}

static uint64_t hash(const char* str)
{
    // TODO
    // Implement a fallback check or collision handling
    // This is not 100% safe because hashing may lead to collisions.
    uint64_t hash = 14695981039346656037ULL;

    while (*str)
    {
        hash ^= (uint8_t)*str++;
        hash *= 1099511628211ULL;
    }

    return hash;
}

static FRESULT index_make_directory(void)
{
    FRESULT fr;
    static const char* directory[] =
    { INDEX_DIR, INDEX_DIR"/.build", INDEX_DIR"/file", INDEX_DIR"/artist",
    INDEX_DIR"/album",
    INDEX_DIR"/genre", INDEX_DIR"/artist/global", INDEX_DIR"/album/global",
    INDEX_DIR"/genre/global" };
    size_t len = sizeof(directory) / sizeof(directory[0]);

    for (int i = 0, size = len; i < size; i++)
    {
        fr = f_mkdir(directory[i]);
        if (fr != FR_OK && fr != FR_EXIST) return fr;
    }

    return FR_OK;
}

static FRESULT ensure_folder_hash_dirs(const uint64_t folder_hash)
{
    FRESULT fr;
    char path[MAX_PATH_LEN];
    int n;

    group_mode_t mode[3] =
    { TIDX_GROUP_BY_ARTIST, TIDX_GROUP_BY_ALBUM, TIDX_GROUP_BY_GENRE };

    for (int i = 0; i < 3; i++)
    {
        n = snprintf(path, sizeof(path), INDEX_DIR"/%s/%016llX", get_mode_name(mode[i]),
                (unsigned long long)folder_hash);
        if (n < 0 || (size_t)n >= sizeof(path)) return FR_INVALID_NAME;

        fr = f_mkdir(path);
        if (fr != FR_OK && fr != FR_EXIST) return fr;
    }

    return FR_OK;
}

static void init_index_header(index_header_t* header)
{
    memset(header, 0, sizeof(*header));

    header->magic = INDEX_MAGIC;
    header->version = INDEX_VERSION;
    header->entry_count = 0;
}

static FRESULT write_index_header(FIL* fp, index_header_t* header)
{
    FRESULT fr;
    UINT bw;

    fr = f_write(fp, header, sizeof(*header), &bw);
    if (fr != FR_OK || bw != sizeof(*header)) return FR_DISK_ERR;

    return FR_OK;
}

static FRESULT scan_directory(const char* dir_path, FIL* fp)
{
    DIR dir;
    FIL folder_fp;
    FILINFO fno;
    FRESULT fr;
    char temp_path[MAX_PATH_LEN];

    snprintf(temp_path, sizeof(temp_path), INDEX_DIR"/.build/%016llX.tmp",
            (unsigned long long)hash(dir_path));

    fr = f_open(&folder_fp, temp_path, FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK) return fr;

    fr = f_opendir(&dir, dir_path);
    if (fr != FR_OK)
    {
        f_close(&folder_fp);
        return fr;
    }

    // First pass: process files in the directory
    while (1)
    {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK) break;
        if (fno.fname[0] == '\0') break;

        if (!(fno.fattrib & AM_DIR) && is_audio_file(fno.fname))
        {
            build_entry_t entry;
            audio_metadata_t metadata;
            UINT bw;

            memset(&entry, 0, sizeof(entry));
            memset(&metadata, 0, sizeof(metadata));

            snprintf(entry.filepath, sizeof(entry.filepath), "%s%s", dir_path, fno.fname);

            // Read audio metadata
            read_audio_metadata(entry.filepath, &metadata);
            strncpy(entry.artist, metadata.artist, MAX_TAG_LEN - 1);
            strncpy(entry.album, metadata.album, MAX_TAG_LEN - 1);
            strncpy(entry.genre, metadata.genre, MAX_TAG_LEN - 1);

            // Write to temp file
            fr = f_write(&folder_fp, &entry, sizeof(entry), &bw);
            if (fr != FR_OK || bw != sizeof(entry))
            {
                fr = FR_DISK_ERR;
                break;
            }

            fr = f_write(fp, &entry, sizeof(entry), &bw);
            if (fr != FR_OK || bw != sizeof(entry))
            {
                fr = FR_DISK_ERR;
                break;
            }
        }
    }
    FRESULT close_fr = f_close(&folder_fp);
    if (fr == FR_OK) fr = close_fr;
    if (fr != FR_OK) return fr;

    fr = f_rewinddir(&dir);
    if (fr != FR_OK)
    {
        f_closedir(&dir);
        return fr;
    }

    // Second pass: recurse and process subdirectories
    while (1)
    {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK) break;
        if (fno.fname[0] == '\0') break;

        if (fno.fattrib & AM_DIR)
        {
            if (strcmp(fno.fname, ".") == 0 || strcmp(fno.fname, "..") == 0) continue;

            char child_path[MAX_PATH_LEN];
            size_t dir_len;
            size_t name_len;

            dir_len = strlen(dir_path);
            name_len = strlen(fno.fname);

            if (dir_len + name_len + 2 >= sizeof(child_path))
            {
                fr = FR_INVALID_NAME;
                break;
            }

            memcpy(child_path, dir_path, dir_len);
            memcpy(child_path + dir_len, fno.fname, name_len);

            child_path[dir_len + name_len] = '/';
            child_path[dir_len + name_len + 1] = '\0';

            fr = scan_directory(child_path, fp);
            if (fr != FR_OK) break;
        }
    }

    close_fr = f_closedir(&dir);
    if (fr == FR_OK) fr = close_fr;
    return fr;
}

static FRESULT build_temp_index(const char* path)
{
    FIL fp;
    FRESULT fr;

    fr = f_open(&fp, INDEX_BUILD_DIR"/global.tmp",
    FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK) return fr;

    fr = scan_directory(path, &fp);
    FRESULT close_fr = f_close(&fp);
    if (fr == FR_OK) fr = close_fr;
    return fr;
}

static uint8_t make_cache_path(char* path, const size_t path_size, const group_mode_t mode,
        const uint8_t global, const uint64_t folder_hash, const uint64_t group_hash)
{
    int n;
    const char* mode_name = get_mode_name(mode);
    if (mode_name == NULL) return 1;

    if (mode == TIDX_GROUP_BY_FILE)
    {
        if (global) n = snprintf(path, path_size, INDEX_DIR"/file/global.tmp");
        else n = snprintf(path, path_size, INDEX_DIR"/file/%016llX.tmp",
                (unsigned long long)folder_hash);
    }
    else
    {
        if (global) n = snprintf(path, path_size, INDEX_DIR"/%s/global/%016llX.tmp", mode_name,
                (unsigned long long)group_hash);
        else n = snprintf(path, path_size, INDEX_DIR"/%s/%016llX/%016llX.tmp", mode_name,
                (unsigned long long)folder_hash, (unsigned long long)group_hash);
    }

    if (n < 0 || (size_t)n >= path_size) return 1;
    return 0;
}

static FRESULT build_file_index(const char* temp_path, const char* index_path)
{
    FIL temp_fp, index_fp;
    FRESULT fr;
    UINT bw, br;
    uint32_t entry_count = 0;
    index_header_t header;

    fr = f_open(&temp_fp, temp_path, FA_READ);
    if (fr != FR_OK) return fr;

    fr = f_open(&index_fp, index_path, FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK)
    {
        f_close(&temp_fp);
        return fr;
    }

    init_index_header(&header);
    fr = write_index_header(&index_fp, &header);
    if (fr != FR_OK)
    {
        f_close(&temp_fp);
        f_close(&index_fp);
        return fr;
    }

    while (1)
    {
        build_entry_t build_entry;
        fr = f_read(&temp_fp, &build_entry, sizeof(build_entry), &br);
        if (fr != FR_OK) break;
        if (br == 0)
        {
            fr = FR_OK;
            break;
        }
        if (br != sizeof(build_entry))
        {
            fr = FR_DISK_ERR;
            break;
        }

        index_entry_t index_entry;
        memset(&index_entry, 0, sizeof(index_entry));
        strncpy(index_entry.filepath, build_entry.filepath, MAX_PATH_LEN - 1);

        fr = f_write(&index_fp, &index_entry, sizeof(index_entry), &bw);
        if (fr != FR_OK || bw != sizeof(index_entry))
        {
            fr = FR_DISK_ERR;
            break;
        }

        entry_count++;
    }
    if (fr != FR_OK)
    {
        f_close(&temp_fp);
        f_close(&index_fp);
        return fr;
    }

    fr = f_close(&temp_fp);
    if (fr != FR_OK)
    {
        f_close(&index_fp);
        return fr;
    }

    if (entry_count == 0)
    {
        fr = f_close(&index_fp);
        if (fr != FR_OK) return fr;
        fr = f_unlink(index_path);
        if (fr != FR_OK && fr != FR_NO_FILE) return fr;

        // Remove the old .idx index
        char final_path[MAX_PATH_LEN];
        size_t len = strlen(index_path);

        if (len < 4 || strcmp(index_path + len - 4, ".tmp") != 0) return FR_INVALID_NAME;

        if (len + 1 > sizeof(final_path)) return FR_INVALID_NAME;

        memcpy(final_path, index_path, len - 4);
        memcpy(final_path + len - 4, ".idx", 5);

        fr = f_unlink(final_path);
        if (fr != FR_OK && fr != FR_NO_FILE) return fr;

        return FR_OK;
    }

    header.entry_count = entry_count;
    fr = f_lseek(&index_fp, 0);
    if (fr == FR_OK) fr = write_index_header(&index_fp, &header);
    if (fr != FR_OK)
    {
        f_close(&index_fp);
        return FR_DISK_ERR;
    }

    fr = f_close(&index_fp);
    if (fr != FR_OK) return fr;

    // Rename .tmp to .idx
    char final_path[MAX_PATH_LEN];
    size_t len = strlen(index_path);
    if (len < 4 || strcmp(index_path + len - 4, ".tmp") != 0) return FR_INVALID_NAME;
    if (len + 1 > sizeof(final_path)) return FR_INVALID_NAME;

    memcpy(final_path, index_path, len - 4);
    memcpy(final_path + len - 4, ".idx", 5);

    fr = f_unlink(final_path);
    if (fr != FR_OK && fr != FR_NO_FILE) return fr;

    fr = f_rename(index_path, final_path);
    return fr;
}

static FRESULT build_group_index(const char* path, const group_mode_t mode, const uint8_t global,
        const uint64_t folder_hash)
{
    FIL temp_fp;
    FRESULT fr;
    UINT br;

    fr = f_open(&temp_fp, path, FA_READ);
    if (fr != FR_OK) return fr;

    while (1)
    {
        index_header_t header;
        build_entry_t entry;
        const char* group_name;
        uint64_t group_hash;
        char index_path[MAX_PATH_LEN];
        FIL index_fp;
        UINT bw;

        fr = f_read(&temp_fp, &entry, sizeof(entry), &br);
        if (fr != FR_OK) break;
        if (br == 0)
        {
            fr = FR_OK;
            break;
        }
        if (br != sizeof(entry))
        {
            fr = FR_DISK_ERR;
            break;
        }

        group_name = get_group_name(&entry, mode);
        if (group_name == NULL || group_name[0] == '\0') continue;

        group_hash = hash(group_name);

        if (make_cache_path(index_path, sizeof(index_path), mode, global, folder_hash, group_hash)
                != 0)
        {
            fr = FR_INVALID_NAME;
            break;
        }

        // Open an existing group index or create a new one
        fr = f_open(&index_fp, index_path, FA_READ | FA_WRITE | FA_OPEN_EXISTING);
        if (fr == FR_NO_FILE)
        {
            fr = f_open(&index_fp, index_path, FA_WRITE | FA_CREATE_ALWAYS);
            if (fr != FR_OK) break;

            init_index_header(&header);
            fr = write_index_header(&index_fp, &header);
            if (fr != FR_OK)
            {
                f_close(&index_fp);
                break;
            }
        }
        else if (fr != FR_OK) break;
        else
        {
            fr = f_read(&index_fp, &header, sizeof(header), &br);
            if (fr != FR_OK || br != sizeof(header))
            {
                f_close(&index_fp);
                fr = FR_DISK_ERR;
                break;
            }

            if (header.magic != INDEX_MAGIC || header.version != INDEX_VERSION)
            {
                f_close(&index_fp);
                fr = FR_INVALID_OBJECT;
                break;
            }
        }

        index_entry_t index_entry;
        memset(&index_entry, 0, sizeof(index_entry));
        strncpy(index_entry.filepath, entry.filepath, MAX_PATH_LEN - 1);

        // Go to EOF and append the track
        fr = f_lseek(&index_fp, f_size(&index_fp));
        if (fr == FR_OK) fr = f_write(&index_fp, &index_entry, sizeof(index_entry), &bw);
        if (fr != FR_OK || bw != sizeof(index_entry))
        {
            f_close(&index_fp);
            fr = FR_DISK_ERR;
            break;
        }

        // Update header
        header.entry_count++;
        fr = f_lseek(&index_fp, 0);

        if (fr == FR_OK) fr = write_index_header(&index_fp, &header);

        if (fr != FR_OK)
        {
            f_close(&index_fp);
            break;
        }

        fr = f_close(&index_fp);
        if (fr != FR_OK) break;
    }

    FRESULT close_fr = f_close(&temp_fp);
    if (fr != FR_OK) return fr;
    if (close_fr != FR_OK) return close_fr;

    // Get current directory
    char directory[MAX_PATH_LEN];
    int n;
    if (global)
    {
        n = snprintf(directory, sizeof(directory),
        INDEX_DIR"/%s/global/", get_mode_name(mode));
    }
    else
    {
        n = snprintf(directory, sizeof(directory),
        INDEX_DIR"/%s/%016llX/", get_mode_name(mode), (unsigned long long)folder_hash);
    }
    if (n < 0 || (size_t)n >= sizeof(directory)) return FR_INVALID_NAME;

    // Remove obsolete .idx files
    {
        DIR dir;
        FILINFO fno;

        fr = f_opendir(&dir, directory);
        if (fr != FR_OK) return fr;

        while (1)
        {
            char index_path[MAX_PATH_LEN];

            fr = f_readdir(&dir, &fno);
            if (fr != FR_OK) break;
            if (fno.fname[0] == '\0')
            {
                fr = FR_OK;
                break;
            }
            if (fno.fattrib & AM_DIR) continue;

            size_t len = strlen(fno.fname);
            if (len < 4 || strcmp(fno.fname + len - 4, ".idx") != 0) continue;

            int n = snprintf(index_path, sizeof(index_path), "%s%s", directory, fno.fname);
            if (n < 0 || (size_t)n >= sizeof(index_path))
            {
                fr = FR_INVALID_NAME;
                break;
            }

            fr = f_unlink(index_path);
            if (fr != FR_OK && fr != FR_NO_FILE) break;
        }

        FRESULT close_fr = f_closedir(&dir);
        if (fr == FR_OK) fr = close_fr;
    }
    if (fr != FR_OK) return fr;

    // Rename all .tmp to .idx
    {
        DIR dir;
        FILINFO fno;

        fr = f_opendir(&dir, directory);
        if (fr != FR_OK) return fr;

        while (1)
        {
            char tmp_path[MAX_PATH_LEN];
            char final_path[MAX_PATH_LEN];
            size_t len;

            fr = f_readdir(&dir, &fno);
            if (fr != FR_OK) break;
            if (fno.fname[0] == '\0')
            {
                fr = FR_OK;
                break;
            }
            if (fno.fattrib & AM_DIR) continue;

            len = strlen(fno.fname);
            if (len < 4 || strcmp(fno.fname + len - 4, ".tmp") != 0) continue;

            n = snprintf(tmp_path, sizeof(tmp_path), "%s%s", directory, fno.fname);
            if (n < 0 || (size_t)n >= sizeof(tmp_path))
            {
                fr = FR_INVALID_NAME;
                break;
            }

            n = snprintf(final_path, sizeof(final_path), "%s%.*s.idx", directory, (int)(len - 4),
                    fno.fname);
            if (n < 0 || (size_t)n >= sizeof(final_path))
            {
                fr = FR_INVALID_NAME;
                break;
            }

            fr = f_unlink(final_path);
            if (fr != FR_OK && fr != FR_NO_FILE) break;

            fr = f_rename(tmp_path, final_path);
            if (fr != FR_OK) break;
        }

        FRESULT close_fr = f_closedir(&dir);
        if (fr == FR_OK) fr = close_fr;
    }

    return fr;
}

static FRESULT build_index(const char* temp_path, const group_mode_t mode, const uint8_t global,
        const uint64_t folder_hash)
{
    if (mode == TIDX_GROUP_BY_FILE)
    {
        char index_path[MAX_PATH_LEN];
        if (make_cache_path(index_path, sizeof(index_path), mode, global, folder_hash, 0) != 0)
            return FR_INVALID_NAME;
        return build_file_index(temp_path, index_path);
    }

    return build_group_index(temp_path, mode, global, folder_hash);
}

static FRESULT build_indexes(const char* temp_path, const uint8_t global,
        const uint64_t folder_hash)
{
    FRESULT fr;
    for (group_mode_t mode = TIDX_GROUP_BY_FILE; mode <= TIDX_GROUP_BY_GENRE; mode++)
    {
        fr = build_index(temp_path, mode, global, folder_hash);
        if (fr != FR_OK) return fr;
    }
    return FR_OK;
}

static FRESULT build_folder_index(const char* temp_path, const uint64_t folder_hash)
{
    FRESULT fr;
    fr = ensure_folder_hash_dirs(folder_hash);
    if (fr != FR_OK) return fr;

    return build_indexes(temp_path, 0, folder_hash);
}

static FRESULT build_global_index(void)
{
    return build_indexes(INDEX_BUILD_DIR"/global.tmp", 1, 0);
}

static FRESULT build_folder_indexes(void)
{
    DIR dir;
    FILINFO fno;
    FRESULT fr;

    fr = f_opendir(&dir, INDEX_BUILD_DIR);
    if (fr != FR_OK) return fr;

    while (1)
    {
        char temp_path[MAX_PATH_LEN];
        uint64_t folder_hash;
        int n;

        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK) break;
        if (fno.fname[0] == '\0')
        {
            fr = FR_OK;
            break;
        }

        if (fno.fattrib & AM_DIR) continue;
        if (strcmp(fno.fname, "global.tmp") == 0) continue;

        n = snprintf(temp_path, sizeof(temp_path), INDEX_BUILD_DIR"/%s", fno.fname);
        if (n < 0 || (size_t)n >= sizeof(temp_path))
        {
            fr = FR_INVALID_NAME;
            break;
        }

        fr = get_folder_hash(temp_path, &folder_hash);
        if (fr != FR_OK) break;

        fr = build_folder_index(temp_path, folder_hash);
        if (fr != FR_OK) break;
    }

    FRESULT close_fr = f_closedir(&dir);
    if (fr == FR_OK) fr = close_fr;
    return fr;
}

FRESULT index_build(void)
{
    FRESULT fr;

    // Make sure ./.Halcyon exists
    fr = index_make_directory();
    if (fr != FR_OK) return fr;

    // Build temp global index cache
    fr = build_temp_index("/MUSIC/");
    if (fr != FR_OK) return fr;

    fr = build_global_index();
    if (fr != FR_OK) return fr;

    fr = build_folder_indexes();
    if (fr != FR_OK) return fr;

    // Build the navigation index
    fr = navigation_build();
    if (fr != FR_OK) return fr;

    return FR_OK;
}

FRESULT index_validate(void)
{
    return FR_OK;
}

FRESULT index_open(index_cursor_t* cursor, const char* path)
{
    FRESULT fr;
    UINT br;

    if (cursor == NULL || path == NULL) return FR_INVALID_PARAMETER;

    memset(cursor, 0, sizeof(*cursor));
    fr = f_open(&cursor->fp, path, FA_READ);
    if (fr != FR_OK) return fr;

    // Read and validate index cache header
    fr = f_read(&cursor->fp, &cursor->header, sizeof(cursor->header), &br);
    if (fr != FR_OK || br != sizeof(cursor->header))
    {
        f_close(&cursor->fp);
        return FR_DISK_ERR;
    }

    if (cursor->header.magic != INDEX_MAGIC || cursor->header.version != INDEX_VERSION)
    {
        f_close(&cursor->fp);
        return FR_INVALID_OBJECT;
    }

    // Make sure the file contains exactly enough space for
    // the number of entries specified by the header.
    if (f_size(&cursor->fp)
            != sizeof(index_header_t)
                    + ((FSIZE_t)cursor->header.entry_count * sizeof(index_entry_t)))
    {
        f_close(&cursor->fp);
        return FR_INVALID_OBJECT;
    }

    cursor->current = 0;
    return FR_OK;
}

FRESULT index_close(index_cursor_t* cursor)
{
    if (cursor == NULL) return FR_INVALID_PARAMETER;
    return f_close(&cursor->fp);
}

FRESULT index_read(index_cursor_t* cursor, uint32_t index, index_entry_t* entry)
{
    FRESULT fr;
    FSIZE_t offset;
    UINT br;

    if (cursor == NULL || entry == NULL) return FR_INVALID_PARAMETER;
    if (index >= cursor->header.entry_count) return FR_INVALID_PARAMETER;

    offset = sizeof(index_header_t) + ((FSIZE_t)index * sizeof(index_entry_t));
    fr = f_lseek(&cursor->fp, offset);
    if (fr != FR_OK) return fr;

    fr = f_read(&cursor->fp, entry, sizeof(*entry), &br);
    if (fr != FR_OK || br != MAX_PATH_LEN) return FR_DISK_ERR;

    return fr;
}
