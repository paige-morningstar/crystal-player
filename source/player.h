#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const char *path;
    uint32_t duration_ms;
    uint32_t audio_start;  // byte offset of the first MPEG frame
    uint16_t bitrate;      // kbit/s, used to seek without decoding the whole file
} PlayerTrack;

bool player_init(void);
void player_shutdown(void);

// Starts a new track immediately (decoding happens on the audio thread).
void player_play(const PlayerTrack *track);

void player_toggle_pause(void);
bool player_paused(void);
bool player_has_track(void);

// True exactly once after the current track reached the end.
bool player_take_finished(void);

void player_seek_to(uint32_t ms);
void player_seek_by(int32_t delta_ms);

uint32_t player_pos_ms(void);
uint32_t player_duration_ms(void);

void player_set_volume(float v);  // 0.0 - 1.0
float player_volume(void);
