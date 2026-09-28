/*
 * player.c
 *
 *  Created on: Sep 15, 2026
 *      Author: Shim Manaloto
 */

#include "player.h"
#include "navigation.h"
#include "track_indexer.h"
#include "wav.h"

static uint32_t folder_index = 0;
static uint32_t group_index = 0;
static uint32_t track_index = 0;

static uint32_t folder_count = 0;
static uint32_t group_count = 0;
static uint32_t track_count = 0;

static player_mode_t mode = PLAYER_MODE_GLOBAL_FILE;
static player_state_t state = PLAYER_STATE_STOPPED;
static I2S_HandleTypeDef* hi2s;

static index_cursor_t cursor;
static uint8_t cursor_valid = 0;

static FRESULT set_group(const player_mode_t new_mode, const uint32_t new_folder_index,
        const uint32_t new_group_index)
{
    FRESULT fr;
    group_mode_t group_mode;
    uint32_t navigation_folder_index;
    index_entry_t leaf_path_entry;
    index_cursor_t new_cursor;
    uint32_t new_folder_count;
    uint32_t new_group_count;

    switch (new_mode)
    {
    case PLAYER_MODE_GLOBAL_FILE:
        group_mode = TIDX_GROUP_BY_FILE;
        navigation_folder_index = UINT32_MAX;
        break;

    case PLAYER_MODE_GLOBAL_ARTIST:
        group_mode = TIDX_GROUP_BY_ARTIST;
        navigation_folder_index = UINT32_MAX;
        break;

    case PLAYER_MODE_GLOBAL_ALBUM:
        group_mode = TIDX_GROUP_BY_ALBUM;
        navigation_folder_index = UINT32_MAX;
        break;

    case PLAYER_MODE_GLOBAL_GENRE:
        group_mode = TIDX_GROUP_BY_GENRE;
        navigation_folder_index = UINT32_MAX;
        break;

    case PLAYER_MODE_FOLDER_FILE:
        group_mode = TIDX_GROUP_BY_FILE;
        navigation_folder_index = new_folder_index;
        break;

    case PLAYER_MODE_FOLDER_ARTIST:
        group_mode = TIDX_GROUP_BY_ARTIST;
        navigation_folder_index = new_folder_index;
        break;

    case PLAYER_MODE_FOLDER_ALBUM:
        group_mode = TIDX_GROUP_BY_ALBUM;
        navigation_folder_index = new_folder_index;
        break;

    case PLAYER_MODE_FOLDER_GENRE:
        group_mode = TIDX_GROUP_BY_GENRE;
        navigation_folder_index = new_folder_index;
        break;

    default:
        return FR_INVALID_PARAMETER;
    }

    fr = navigation_read(group_mode, navigation_folder_index, new_group_index, &leaf_path_entry,
            &new_folder_count, &new_group_count);
    if (fr != FR_OK) return fr;

    fr = index_open(&new_cursor, leaf_path_entry.filepath);
    if (fr != FR_OK) return fr;

    if (new_cursor.header.entry_count == 0)
    {
        index_close(&new_cursor);
        return FR_NO_FILE;
    }

    if (cursor_valid) index_close(&cursor);

    cursor = new_cursor;
    cursor_valid = 1;

    mode = new_mode;

    folder_index = new_folder_index;
    folder_count = new_folder_count;

    group_index = new_group_index;
    group_count = new_group_count;

    track_index = 0;
    track_count = cursor.header.entry_count;

    return FR_OK;
}

static uint32_t compute_next_track_index(void)
{
    return (track_index + 1) % track_count;
}

static uint32_t compute_prev_track_index(void)
{
    return (track_index == 0) ? (track_count - 1) : (track_index - 1);
}

static uint32_t compute_next_group_index(void)
{
    return (group_index + 1) % group_count;
}

static uint32_t compute_prev_group_index(void)
{
    return (group_index == 0) ? (group_count - 1) : (group_index - 1);
}

static uint32_t compute_next_folder_index(void)
{
    return (folder_index + 1) % folder_count;
}

static uint32_t compute_prev_folder_index(void)
{
    return (folder_index == 0) ? (folder_count - 1) : (folder_index - 1);
}

static player_mode_t compute_next_mode(void)
{
    if (mode == PLAYER_MODE_FOLDER_GENRE) return PLAYER_MODE_GLOBAL_FILE;

    return (player_mode_t)(mode + 1);
}

static player_mode_t compute_prev_mode(void)
{
    if (mode == PLAYER_MODE_GLOBAL_FILE) return PLAYER_MODE_FOLDER_GENRE;

    return (player_mode_t)(mode - 1);
}

static void load_and_play(uint32_t new_track_index)
{
    FRESULT fr;
    index_entry_t track_entry;

    if (!cursor_valid) return;

    fr = index_read(&cursor, new_track_index, &track_entry);
    if (fr != FR_OK) return;

    audio_play(track_entry.filepath, hi2s);

    track_index = new_track_index;
    state = PLAYER_STATE_PLAYING;
}

void player_init(I2S_HandleTypeDef* i2s_handle)
{
    hi2s = i2s_handle;
    set_group(PLAYER_MODE_GLOBAL_FILE, UINT32_MAX, 0);
    state = PLAYER_STATE_STOPPED;
}

void player_play(void)
{
    if (state == PLAYER_STATE_PAUSED)
    {
        audio_resume();
        state = PLAYER_STATE_PLAYING;
        return;
    }
    if (state == PLAYER_STATE_STOPPED)
    {
        load_and_play(track_index);
        return;
    }
}

void player_pause(void)
{
    if (state != PLAYER_STATE_PLAYING) return;

    audio_pause();
    state = PLAYER_STATE_PAUSED;
}

void player_stop(void)
{
    if (state == PLAYER_STATE_STOPPED) return;

    audio_stop();
    state = PLAYER_STATE_STOPPED;
}

void player_next_track(void)
{
    load_and_play(compute_next_track_index());
}
void player_prev_track(void)
{
    load_and_play(compute_prev_track_index());
}

void player_next_mode(void)
{
    player_mode_t next_mode = compute_next_mode();
    set_group(next_mode, 0, 0);
}

void player_prev_mode(void)
{
    player_mode_t prev_mode = compute_prev_mode();
    set_group(prev_mode, 0, 0);
}

void player_next_folder(void)
{
    set_group(mode, compute_next_folder_index(), group_index);
}

void player_prev_folder(void)
{
    set_group(mode, compute_prev_folder_index(), group_index);
}

void player_next_group(void)
{
    set_group(mode, folder_index, compute_next_group_index());
}

void player_prev_group(void)
{
    set_group(mode, folder_index, compute_prev_group_index());
}
