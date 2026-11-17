#pragma once

#include <stdint.h>

typedef struct {
    uint32_t duration_ms;
    uint16_t bitrate;      // kbit/s
    uint32_t sample_rate;  // Hz
    uint8_t  channels;
    uint32_t audio_start;  // byte offset of the first audio frame
} Mp3Info;

// Cheaply probes an MP3: reads the first frame header (skipping ID3v2) and the
// Xing/Info header when present, so we get a duration without decoding.
void mp3_probe(const char *path, Mp3Info *out);
