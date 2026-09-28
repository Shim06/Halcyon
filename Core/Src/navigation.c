#include "navigation.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static FRESULT write_header(FIL* fp, const navigation_index_header_t* header)
{
    FRESULT fr;
    UINT bw;
    fr = f_write(fp, header, sizeof(*header), &bw);
    if (fr != FR_OK || bw != sizeof(*header))
    {
        f_close(fp);
        return FR_DISK_ERR;
    }
    return FR_OK;
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

static FRESULT count_folders(uint32_t* file_count, uint32_t* artist_count, uint32_t* album_count,
        uint32_t* genre_count)
{
    DIR dir;
    FILINFO fno;
    FRESULT fr;
    uint32_t* count;
    const char* mode_name;
    char directory[MAX_PATH_LEN];
    int n;

    if (file_count == NULL || artist_count == NULL || album_count == NULL || genre_count == NULL)
        return FR_INVALID_PARAMETER;

    *file_count = 0;
    *artist_count = 0;
    *album_count = 0;
    *genre_count = 0;

    for (group_mode_t mode = TIDX_GROUP_BY_FILE; mode <= TIDX_GROUP_BY_GENRE; mode++)
    {
        mode_name = get_mode_name(mode);
        if (mode_name == NULL) return FR_INVALID_PARAMETER;

        n = snprintf(directory, sizeof(directory),
        INDEX_DIR"/%s/", mode_name);
        if (n < 0 || (size_t)n >= sizeof(directory)) return FR_INVALID_NAME;

        if (mode == TIDX_GROUP_BY_FILE) count = file_count;
        else if (mode == TIDX_GROUP_BY_ARTIST) count = artist_count;
        else if (mode == TIDX_GROUP_BY_ALBUM) count = album_count;
        else count = genre_count;

        fr = f_opendir(&dir, directory);
        if (fr != FR_OK) return fr;

        while (1)
        {
            fr = f_readdir(&dir, &fno);
            if (fr != FR_OK) break;
            if (fno.fname[0] == '\0')
            {
                fr = FR_OK;
                break;
            }
            if (mode == TIDX_GROUP_BY_FILE)
            {
                // FILE mode contains <folder-hash>.idx files directly, no subdirectories.
                if (fno.fattrib & AM_DIR) continue;
                if (strcmp(fno.fname, "global.idx") == 0) continue;
                if (!strstr(fno.fname, ".idx")) continue;
            }
            else
            {
                if (!(fno.fattrib & AM_DIR)) continue;
                if (strcmp(fno.fname, "global") == 0) continue;
            }

            (*count)++;
        }

        FRESULT close_fr = f_closedir(&dir);
        if (fr == FR_OK) fr = close_fr;
        if (fr != FR_OK) return fr;
    }

    return FR_OK;
}

static FRESULT write_path(FIL* fp, const char* path)
{
    index_entry_t entry;
    UINT bw;

    memset(&entry, 0, sizeof(entry));
    strncpy(entry.filepath, path, MAX_PATH_LEN - 1U);

    if (f_write(fp, &entry, sizeof(entry), &bw) != FR_OK) return FR_DISK_ERR;
    if (bw != sizeof(entry)) return FR_DISK_ERR;

    return FR_OK;
}

static FRESULT index_build_global(FIL* fp, const group_mode_t mode, uint32_t* count,
        uint32_t* table_offset)
{
    DIR dir;
    FILINFO fno;
    FRESULT fr;
    UINT bw;
    uint32_t entry_count = 0;
    char directory[MAX_PATH_LEN];
    char path[MAX_PATH_LEN];
    int n;

    if (count == NULL || table_offset == NULL || fp == NULL) return FR_INVALID_PARAMETER;
    if (mode < TIDX_GROUP_BY_ARTIST || mode > TIDX_GROUP_BY_GENRE) return FR_INVALID_PARAMETER;

    const char* mode_name = get_mode_name(mode);
    if (mode_name == NULL) return FR_INVALID_PARAMETER;

    *table_offset = (uint32_t)f_tell(fp);

    n = snprintf(directory, sizeof(directory), INDEX_DIR"/%s/global/", mode_name);
    if (n < 0 || (size_t)n >= sizeof(directory)) return FR_INVALID_NAME;

    fr = f_opendir(&dir, directory);
    if (fr != FR_OK) return fr;

    while (1)
    {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK) break;
        if (fno.fname[0] == '\0')
        {
            fr = FR_OK;
            break;
        }
        if (fno.fattrib & AM_DIR) continue;
        if (!strstr(fno.fname, ".idx")) continue;

        memset(path, 0, sizeof(path));
        n = snprintf(path, sizeof(path), INDEX_DIR"/%s/global/%s", mode_name, fno.fname);
        if (n < 0 || (size_t)n >= sizeof(path))
        {
            fr = FR_INVALID_NAME;
            break;
        }

        fr = f_write(fp, path, MAX_PATH_LEN, &bw);
        if (fr != FR_OK || bw != MAX_PATH_LEN)
        {
            fr = FR_DISK_ERR;
            break;
        }

        entry_count++;
    }
    FRESULT close_fr = f_closedir(&dir);
    if (fr == FR_OK) fr = close_fr;
    if (fr != FR_OK) return fr;

    *count = entry_count;
    return FR_OK;
}

static FRESULT index_build_folders(FIL* fp, const group_mode_t mode, const uint32_t folder_count,
        const uint32_t folder_table_offset)
{
    DIR dir;
    FILINFO fno;
    FRESULT fr;
    UINT bw;

    char directory[MAX_PATH_LEN];
    char path[MAX_PATH_LEN];

    const char* mode_name;
    uint32_t folder_index = 0;
    uint32_t folder_offset;
    uint32_t entry_count;

    navigation_folder_t folder;

    int n;

    if (fp == NULL) return FR_INVALID_PARAMETER;
    if (mode < TIDX_GROUP_BY_FILE || mode > TIDX_GROUP_BY_GENRE) return FR_INVALID_PARAMETER;

    mode_name = get_mode_name(mode);
    if (mode_name == NULL) return FR_INVALID_PARAMETER;

    if (mode == TIDX_GROUP_BY_FILE)
    {
        n = snprintf(directory, sizeof(directory),
        INDEX_DIR"/file/");
    }
    else
    {
        n = snprintf(directory, sizeof(directory),
        INDEX_DIR"/%s/", mode_name);
    }
    if (n < 0 || (size_t)n >= sizeof(directory)) return FR_INVALID_NAME;

    fr = f_opendir(&dir, directory);
    if (fr != FR_OK) return fr;

    while (1)
    {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK) break;
        if (fno.fname[0] == '\0')
        {
            fr = FR_OK;
            break;
        }

        // FILE mode contains <folder-hash>.idx files directly
        if (mode == TIDX_GROUP_BY_FILE)
        {
            if (fno.fattrib & AM_DIR) continue;
            if (strcmp(fno.fname, "global.idx") == 0) continue;
            if (!strstr(fno.fname, ".idx")) continue;
            if (folder_index >= folder_count)
            {
                fr = FR_INVALID_OBJECT;
                break;
            }

            n = snprintf(path, sizeof(path), INDEX_DIR"/file/%s", fno.fname);
            if (n < 0 || (size_t)n >= sizeof(path))
            {
                fr = FR_INVALID_NAME;
                break;
            }

            fr = f_lseek(fp, folder_table_offset + (FSIZE_t)folder_index * MAX_PATH_LEN);
            if (fr != FR_OK) break;

            fr = f_write(fp, path, MAX_PATH_LEN, &bw);
            if (fr != FR_OK || bw != MAX_PATH_LEN)
            {
                fr = FR_DISK_ERR;
                break;
            }

            folder_index++;
            continue;
        }

        // All other modes contain one directory per folder
        if (!(fno.fattrib & AM_DIR)) continue;
        if (strcmp(fno.fname, "global") == 0) continue;
        if (folder_index >= folder_count)
        {
            fr = FR_INVALID_OBJECT;
            break;
        }

        n = snprintf(directory, sizeof(directory), INDEX_DIR"/%s/%s/", mode_name, fno.fname);
        if (n < 0 || (size_t)n >= sizeof(directory))
        {
            fr = FR_INVALID_NAME;
            break;
        }

        folder_offset = (uint32_t)f_tell(fp);
        memset(&folder, 0, sizeof(folder));

        folder.mode = mode;
        folder.entry_table_offset = folder_offset + sizeof(folder);

        // Count the .idx files in the folder
        DIR entry_dir;
        FILINFO entry_fno;
        entry_count = 0;

        fr = f_opendir(&entry_dir, directory);
        if (fr != FR_OK) break;

        while (1)
        {
            fr = f_readdir(&entry_dir, &entry_fno);
            if (fr != FR_OK) break;
            if (entry_fno.fname[0] == '\0')
            {
                fr = FR_OK;
                break;
            }
            if (entry_fno.fattrib & AM_DIR) continue;
            if (!strstr(entry_fno.fname, ".idx")) continue;

            entry_count++;
        }

        FRESULT close_fr = f_closedir(&entry_dir);
        if (fr == FR_OK) fr = close_fr;
        if (fr != FR_OK) break;

        folder.entry_count = entry_count;

        // Write the folder record
        fr = f_write(fp, &folder, sizeof(folder), &bw);
        if (fr != FR_OK || bw != sizeof(folder))
        {
            fr = FR_DISK_ERR;
            break;
        }

        // Write all the idx paths
        fr = f_opendir(&entry_dir, directory);
        if (fr != FR_OK) break;

        while (1)
        {
            fr = f_readdir(&entry_dir, &entry_fno);
            if (fr != FR_OK) break;
            if (entry_fno.fname[0] == '\0')
            {
                fr = FR_OK;
                break;
            }
            if (entry_fno.fattrib & AM_DIR) continue;
            if (!strstr(entry_fno.fname, ".idx")) continue;

            n = snprintf(path, sizeof(path), "%s%s", directory, entry_fno.fname);
            if (n < 0 || (size_t)n >= sizeof(path))
            {
                fr = FR_INVALID_NAME;
                break;
            }

            fr = f_write(fp, path, MAX_PATH_LEN, &bw);
            if (fr != FR_OK || bw != MAX_PATH_LEN)
            {
                fr = FR_DISK_ERR;
                break;
            }
        }

        close_fr = f_closedir(&entry_dir);
        if (fr == FR_OK) fr = close_fr;
        if (fr != FR_OK) break;

        // Patch the folder's offset into the folder table.
        fr = f_lseek(fp, folder_table_offset + folder_index * sizeof(uint32_t));
        if (fr != FR_OK) break;

        fr = f_write(fp, &folder_offset, sizeof(folder_offset), &bw);
        if (fr != FR_OK || bw != sizeof(folder_offset))
        {
            fr = FR_DISK_ERR;
            break;
        }

        // Return to the end of the folder data
        fr = f_lseek(fp, folder.entry_table_offset + folder.entry_count * MAX_PATH_LEN);
        if (fr != FR_OK) break;

        folder_index++;
    }

    FRESULT close_fr = f_closedir(&dir);
    if (fr == FR_OK) fr = close_fr;
    if (fr != FR_OK) return fr;
    if (folder_index != folder_count) return FR_INVALID_OBJECT;

    return FR_OK;
}

static FRESULT navigation_read_folder(FIL* fp, uint32_t folder_index,
        const navigation_index_header_t* header, const group_mode_t mode,
        navigation_folder_t* folder)
{
    FRESULT fr;
    UINT br;
    uint32_t folder_offset;
    uint32_t folder_count;
    uint32_t folder_table_offset;

    if (fp == NULL || folder == NULL || header == NULL) return FR_INVALID_PARAMETER;

    if (mode == TIDX_GROUP_BY_ARTIST)
    {
        folder_count = header->folder_artist_count;
        folder_table_offset = header->folder_artist_table_offset;
    }
    else if (mode == TIDX_GROUP_BY_ALBUM)
    {
        folder_count = header->folder_album_count;
        folder_table_offset = header->folder_album_table_offset;
    }
    else if (mode == TIDX_GROUP_BY_GENRE)
    {
        folder_count = header->folder_genre_count;
        folder_table_offset = header->folder_genre_table_offset;
    }
    else return FR_INVALID_PARAMETER;
    if (folder_count == 0) return FR_NO_FILE;
    if (folder_index >= folder_count) return FR_INVALID_PARAMETER;

    fr = f_lseek(fp, folder_table_offset + folder_index * sizeof(uint32_t));
    if (fr != FR_OK) return fr;

    fr = f_read(fp, &folder_offset, sizeof(folder_offset), &br);
    if (fr != FR_OK) return fr;
    if (br != sizeof(folder_offset)) return FR_DISK_ERR;

    fr = f_lseek(fp, folder_offset);
    if (fr != FR_OK) return fr;

    fr = f_read(fp, folder, sizeof(*folder), &br);
    if (fr != FR_OK) return fr;
    if (br != sizeof(*folder)) return FR_DISK_ERR;

    if (folder->mode != mode) return FR_INVALID_OBJECT;

    return FR_OK;

}

static FRESULT navigation_resolve_index_path(FIL* fp, const navigation_index_header_t* header,
        const group_mode_t mode, const uint32_t folder_index, const uint32_t mode_entry_index,
        char* path)
{
    uint32_t entry_offset;
    uint32_t count;
    uint32_t table_offset;
    navigation_folder_t folder;
    UINT br;
    FRESULT fr;

    if (fp == NULL || header == NULL || path == NULL) return FR_INVALID_PARAMETER;

    if (mode == TIDX_GROUP_BY_FILE)
    {
        if (folder_index == UINT32_MAX) entry_offset = header->global_file_offset;
        else
        {
            if (folder_index >= header->folder_file_count) return FR_INVALID_PARAMETER;

            entry_offset = header->folder_file_table_offset + folder_index * MAX_PATH_LEN;
        }
    }
    else
    {
        if (folder_index == UINT32_MAX)
        {
            if (mode == TIDX_GROUP_BY_ARTIST)
            {
                count = header->global_artist_count;
                table_offset = header->global_artist_table_offset;
            }
            else if (mode == TIDX_GROUP_BY_ALBUM)
            {
                count = header->global_album_count;
                table_offset = header->global_album_table_offset;
            }
            else if (mode == TIDX_GROUP_BY_GENRE)
            {
                count = header->global_genre_count;
                table_offset = header->global_genre_table_offset;
            }
            else return FR_INVALID_PARAMETER;

            if (count == 0) return FR_NO_FILE;
            if (mode_entry_index >= count) return FR_INVALID_PARAMETER;

            entry_offset = table_offset + mode_entry_index * MAX_PATH_LEN;
        }
        else
        {
            fr = navigation_read_folder(fp, folder_index, header, mode, &folder);
            if (fr != FR_OK) return fr;
            if (folder.entry_count == 0) return FR_NO_FILE;
            if (mode_entry_index >= folder.entry_count) return FR_INVALID_PARAMETER;

            entry_offset = folder.entry_table_offset + mode_entry_index * MAX_PATH_LEN;
        }
    }

    // Read the actual .idx path
    fr = f_lseek(fp, entry_offset);
    if (fr != FR_OK) return fr;

    fr = f_read(fp, path, MAX_PATH_LEN, &br);
    if (fr != FR_OK) return fr;
    if (br != MAX_PATH_LEN) return FR_DISK_ERR;

    path[MAX_PATH_LEN - 1] = '\0';

    return FR_OK;
}

FRESULT navigation_build(void)
{
    FRESULT fr;
    FIL fp;
    UINT bw;
    navigation_index_header_t header;

    uint32_t folder_file_count;
    uint32_t folder_artist_count;
    uint32_t folder_album_count;
    uint32_t folder_genre_count;

    uint32_t folder_file_table_offset;
    uint32_t folder_artist_table_offset;
    uint32_t folder_album_table_offset;
    uint32_t folder_genre_table_offset;

    fr = count_folders(&folder_file_count, &folder_artist_count, &folder_album_count,
            &folder_genre_count);
    if (fr != FR_OK) return fr;

    memset(&header, 0, sizeof(header));

    header.magic = NAVIGATION_MAGIC;
    header.version = NAVIGATION_VERSION;

    header.folder_file_count = folder_file_count;
    header.folder_artist_count = folder_artist_count;
    header.folder_album_count = folder_album_count;
    header.folder_genre_count = folder_genre_count;

    fr = f_open(&fp, NAVIGATION_PATH, FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK) return fr;

    fr = write_header(&fp, &header);
    if (fr != FR_OK) return fr;

    // Global file index
    header.global_file_offset = (uint32_t)f_tell(&fp);
    fr = write_path(&fp, INDEX_DIR"/file/global.idx");
    if (fr != FR_OK)
    {
        f_close(&fp);
        return fr;
    }

    // Global artist/album/genre tables
    {
        uint32_t count;
        uint32_t table_offset;
        group_mode_t mode[3] =
        { TIDX_GROUP_BY_ARTIST, TIDX_GROUP_BY_ALBUM, TIDX_GROUP_BY_GENRE };
        for (int i = 0; i < 3; i++)
        {
            fr = index_build_global(&fp, mode[i], &count, &table_offset);
            if (fr != FR_OK)
            {
                f_close(&fp);
                return fr;
            }

            if (mode[i] == TIDX_GROUP_BY_ARTIST)
            {
                header.global_artist_count = count;
                header.global_artist_table_offset = table_offset;
            }
            else if (mode[i] == TIDX_GROUP_BY_ALBUM)
            {
                header.global_album_count = count;
                header.global_album_table_offset = table_offset;
            }
            else
            {
                header.global_genre_count = count;
                header.global_genre_table_offset = table_offset;
            }
        }
    }

    // Reserve folder tables
    {
        static const char zero_path[MAX_PATH_LEN] =
        { 0 };
        uint32_t zero = 0;
        group_mode_t mode[4] =
        { TIDX_GROUP_BY_FILE, TIDX_GROUP_BY_ARTIST, TIDX_GROUP_BY_ALBUM, TIDX_GROUP_BY_GENRE };
        uint32_t folder_count[4] =
        { folder_file_count, folder_artist_count, folder_album_count, folder_genre_count };
        for (int i = 0; i < 4; i++)
        {
            if (mode[i] == TIDX_GROUP_BY_FILE)
            {
                folder_file_table_offset = (uint32_t)f_tell(&fp);
                header.folder_file_table_offset = folder_file_table_offset;
            }
            else if (mode[i] == TIDX_GROUP_BY_ARTIST)
            {
                folder_artist_table_offset = (uint32_t)f_tell(&fp);
                header.folder_artist_table_offset = folder_artist_table_offset;
            }
            else if (mode[i] == TIDX_GROUP_BY_ALBUM)
            {
                folder_album_table_offset = (uint32_t)f_tell(&fp);
                header.folder_album_table_offset = folder_album_table_offset;
            }
            else
            {
                folder_genre_table_offset = (uint32_t)f_tell(&fp);
                header.folder_genre_table_offset = folder_genre_table_offset;
            }

            for (uint32_t j = 0, count = folder_count[i]; j < count; j++)
            {
                if (mode[i] == TIDX_GROUP_BY_FILE) fr = f_write(&fp, zero_path, sizeof(zero_path),
                        &bw);
                else fr = f_write(&fp, &zero, sizeof(zero), &bw);

                UINT expected = (mode[i] == TIDX_GROUP_BY_FILE) ? sizeof(zero_path) : sizeof(zero);
                if (fr != FR_OK || bw != expected)
                {
                    f_close(&fp);
                    return FR_DISK_ERR;
                }
            }
        }
    }

    // Build all folder records
    {
        group_mode_t mode[4] =
        { TIDX_GROUP_BY_FILE, TIDX_GROUP_BY_ARTIST, TIDX_GROUP_BY_ALBUM, TIDX_GROUP_BY_GENRE };
        uint32_t folder_count[4] =
        { folder_file_count, folder_artist_count, folder_album_count, folder_genre_count };
        uint32_t folder_table_offset[4] =
        { folder_file_table_offset, folder_artist_table_offset, folder_album_table_offset,
                folder_genre_table_offset };
        for (int i = 0; i < 4; i++)
        {
            fr = index_build_folders(&fp, mode[i], folder_count[i], folder_table_offset[i]);
            if (fr != FR_OK)
            {
                f_close(&fp);
                return fr;
            }
        }
    }

    // Rewrite the header now that all offsets/counts are known
    fr = f_lseek(&fp, 0);
    if (fr == FR_OK) fr = f_write(&fp, &header, sizeof(header), &bw);

    if (fr != FR_OK || bw != sizeof(header))
    {
        f_close(&fp);
        return FR_DISK_ERR;
    }

    fr = f_close(&fp);
    return fr;
}

FRESULT navigation_read(const group_mode_t mode, const uint32_t folder_index,
        const uint32_t mode_entry_index, index_entry_t* entry, uint32_t* folder_count,
        uint32_t* group_count)
{
    FIL navigation_fp;
    navigation_index_header_t header;
    navigation_folder_t folder;
    UINT br;
    FRESULT fr;

    if (entry == NULL || folder_count == NULL || group_count == NULL) return FR_INVALID_PARAMETER;

    // Open navigation.idx
    fr = f_open(&navigation_fp, NAVIGATION_PATH, FA_READ);
    if (fr != FR_OK) return fr;

    // Read header
    fr = f_read(&navigation_fp, &header, sizeof(header), &br);
    if (fr != FR_OK || br != sizeof(header))
    {
        f_close(&navigation_fp);
        return FR_DISK_ERR;
    }

    // Check if file header is valid
    if (header.magic != NAVIGATION_MAGIC || header.version != NAVIGATION_VERSION)
    {
        f_close(&navigation_fp);
        return FR_INVALID_OBJECT;
    }

    // Get folder count
    if (folder_index == UINT32_MAX) *folder_count = 1;
    else if (mode == TIDX_GROUP_BY_FILE) *folder_count = header.folder_file_count;
    else if (mode == TIDX_GROUP_BY_ARTIST) *folder_count = header.folder_artist_count;
    else if (mode == TIDX_GROUP_BY_ALBUM) *folder_count = header.folder_album_count;
    else if (mode == TIDX_GROUP_BY_GENRE) *folder_count = header.folder_genre_count;
    else
    {
        f_close(&navigation_fp);
        return FR_INVALID_PARAMETER;
    }

    // Get group count
    if (folder_index == UINT32_MAX)
    {
        if (mode == TIDX_GROUP_BY_FILE) *group_count = 1;
        else if (mode == TIDX_GROUP_BY_ARTIST) *group_count = header.global_artist_count;
        else if (mode == TIDX_GROUP_BY_ALBUM) *group_count = header.global_album_count;
        else if (mode == TIDX_GROUP_BY_GENRE) *group_count = header.global_genre_count;
        else
        {
            f_close(&navigation_fp);
            return FR_INVALID_PARAMETER;
        }
    }
    else if (mode == TIDX_GROUP_BY_FILE)
    {
        if (folder_index >= header.folder_file_count)
        {
            f_close(&navigation_fp);
            return FR_INVALID_PARAMETER;
        }

        *group_count = 1;
    }
    else
    {
        fr = navigation_read_folder(&navigation_fp, folder_index, &header, mode, &folder);
        if (fr != FR_OK)
        {
            f_close(&navigation_fp);
            return fr;
        }

        *group_count = folder.entry_count;
    }

    fr = navigation_resolve_index_path(&navigation_fp, &header, mode, folder_index,
            mode_entry_index, entry->filepath);
    if (fr != FR_OK)
    {
        f_close(&navigation_fp);
        return fr;
    }

    fr = f_close(&navigation_fp);
    if (fr != FR_OK) return fr;

    return FR_OK;
}
