/*
 * player.h
 *
 *  Created on: Sep 15, 2026
 *      Author: Shim Manaloto
 */

#ifndef INC_PLAYER_H_
#define INC_PLAYER_H_

#include "stm32f4xx_hal.h"

typedef enum
{
    PLAYER_STATE_STOPPED,
    PLAYER_STATE_PLAYING,
    PLAYER_STATE_PAUSED
} player_state_t;

typedef enum
{
    PLAYER_MODE_GLOBAL_FILE = 0,
    PLAYER_MODE_GLOBAL_ARTIST,
    PLAYER_MODE_GLOBAL_ALBUM,
    PLAYER_MODE_GLOBAL_GENRE,
    PLAYER_MODE_FOLDER_FILE,
    PLAYER_MODE_FOLDER_ARTIST,
    PLAYER_MODE_FOLDER_ALBUM,
    PLAYER_MODE_FOLDER_GENRE
} player_mode_t;

void player_init(I2S_HandleTypeDef* i2s_handle);
void player_play(void);
void player_pause(void);
void player_stop(void);

void player_next_track(void);
void player_prev_track(void);

void player_next_mode(void);
void player_prev_mode(void);

void player_next_folder(void);
void player_prev_folder(void);

void player_next_group(void);
void player_prev_group(void);

#endif /* INC_PLAYER_H_ */
