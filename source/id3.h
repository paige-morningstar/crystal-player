#pragma once

#include <stdbool.h>
#include <stdint.h>

// Embedded cover art formats we know how to decode.
enum {
    ART_NONE = 0,
    ART_JPEG = 1,
    ART_PNG  = 2,
};

typedef struct {
    char     title[160];
    char     artist[160];
    char     album[160];
    char     album_artist[160];
    uint16_t track_no;
    uint16_t disc_no;
    uint16_t year;
    uint32_t tlen_ms;      // TLEN frame, often bogus; only used as a last resort
    // Location of the embedded cover picture inside the file.
    uint32_t art_offset;
    uint32_t art_size;
    uint8_t  art_type;     // ART_*
} Id3Tags;

// Reads ID3v2.2/2.3/2.4 and falls back to ID3v1. Never fails hard: on any
// problem the tag fields are simply left empty.
void id3_read(const char *path, Id3Tags *out);
