#include "mp3info.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int      version;            // 1, 2 or 25 (MPEG 2.5)
    int      layer;              // 1, 2 or 3
    int      bitrate;
    uint32_t sample_rate;
    int      channels;
    int      samples_per_frame;
    int      frame_size;         // bytes, for this frame
} Mp3Header;

static const int kBitrates[2][3][15] = {
    { // MPEG 1
      {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448},
      {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384},
      {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320},
    },
    { // MPEG 2 / 2.5
      {0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256},
      {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160},
      {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160},
    },
};

static const uint32_t kSampleRates[3][3] = {
    {44100, 48000, 32000}, // MPEG 1
    {22050, 24000, 16000}, // MPEG 2
    {11025, 12000, 8000},  // MPEG 2.5
};

static int parse_header(uint32_t h, Mp3Header *o)
{
    if ((h & 0xFFE00000u) != 0xFFE00000u)
        return 0;

    int versionBits = (int)((h >> 19) & 3);
    if (versionBits == 1)
        return 0;
    int layerBits = (int)((h >> 17) & 3);
    if (layerBits == 0)
        return 0;
    int brIndex = (int)((h >> 12) & 0xF);
    if (brIndex == 0 || brIndex == 15)
        return 0;
    int srIndex = (int)((h >> 10) & 3);
    if (srIndex == 3)
        return 0;

    int padding = (int)((h >> 9) & 1);
    int mode = (int)((h >> 6) & 3);

    o->version = (versionBits == 3) ? 1 : ((versionBits == 2) ? 2 : 25);
    o->layer = 4 - layerBits;
    int table = (o->version == 1) ? 0 : 1;
    o->bitrate = kBitrates[table][o->layer - 1][brIndex];
    o->sample_rate = kSampleRates[o->version == 1 ? 0 : (o->version == 2 ? 1 : 2)][srIndex];
    o->channels = (mode == 3) ? 1 : 2;
    if (o->bitrate <= 0 || o->sample_rate == 0)
        return 0;

    if (o->layer == 1)
        o->samples_per_frame = 384;
    else if (o->layer == 2)
        o->samples_per_frame = 1152;
    else
        o->samples_per_frame = (o->version == 1) ? 1152 : 576;

    if (o->layer == 1)
        o->frame_size = (12 * o->bitrate * 1000 / (int)o->sample_rate + padding) * 4;
    else
        o->frame_size = (o->samples_per_frame / 8) * o->bitrate * 1000 / (int)o->sample_rate + padding;

    return o->frame_size > 4;
}

static uint32_t read_be32(const unsigned char *b)
{
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}

void mp3_probe(const char *path, Mp3Info *out)
{
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    if (!path)
        return;

    FILE *fp = fopen(path, "rb");
    if (!fp)
        return;

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return;
    }
    long fileSize = ftell(fp);
    if (fileSize <= 0) {
        fclose(fp);
        return;
    }

    enum { WINDOW = 65536 };
    unsigned char *buf = (unsigned char *)malloc(WINDOW);
    if (!buf) {
        fclose(fp);
        return;
    }

    // Cover art can push the ID3v2 tag well past one window, so the frames have
    // to be looked for from where the tag says the audio starts.
    long audioStart = 0;
    if (fseek(fp, 0, SEEK_SET) == 0) {
        size_t head = fread(buf, 1, WINDOW, fp);
        if (head >= 10 && memcmp(buf, "ID3", 3) == 0) {
            uint32_t tagSize = ((uint32_t)(buf[6] & 0x7F) << 21) | ((uint32_t)(buf[7] & 0x7F) << 14) |
                               ((uint32_t)(buf[8] & 0x7F) << 7) | (uint32_t)(buf[9] & 0x7F);
            audioStart = 10 + (long)tagSize + ((buf[5] & 0x10) ? 10 : 0); // 0x10 = footer
            if (audioStart < 0 || audioStart > fileSize - 4)
                audioStart = 10;
        }
    }

    bool hasV1 = false;
    if (fileSize > 128) {
        if (fseek(fp, fileSize - 128, SEEK_SET) == 0) {
            unsigned char tail[3];
            if (fread(tail, 1, 3, fp) == 3 && memcmp(tail, "TAG", 3) == 0)
                hasV1 = true;
        }
    }

    // Find a frame header, requiring the following frame to line up too. If the
    // tag size was wrong, retry from the start of the file.
    Mp3Header hdr;
    memset(&hdr, 0, sizeof(hdr));
    long framePos = -1;
    long windowBase = 0;
    size_t windowLen = 0;

    for (int attempt = 0; attempt < 2 && framePos < 0; attempt++) {
        long from = (attempt == 0) ? audioStart : 0;
        if (from < 0 || from > fileSize - 8)
            continue;
        if (attempt == 1 && audioStart == 0)
            continue;
        if (fseek(fp, from, SEEK_SET) != 0)
            continue;

        size_t got = fread(buf, 1, WINDOW, fp);
        for (size_t i = 0; i + 4 < got; i++) {
            uint32_t h = read_be32(buf + i);
            if (!parse_header(h, &hdr))
                continue;
            size_t next = i + (size_t)hdr.frame_size;
            if (next + 4 <= got) {
                Mp3Header h2;
                if (!parse_header(read_be32(buf + next), &h2) || h2.sample_rate != hdr.sample_rate)
                    continue;
            }
            framePos = from + (long)i;
            windowBase = from;
            windowLen = got;
            break;
        }
    }

    if (framePos < 0) {
        free(buf);
        fclose(fp);
        return;
    }

    long audioBytes = fileSize - framePos - (hasV1 ? 128 : 0);

    out->bitrate = (uint16_t)hdr.bitrate;
    out->sample_rate = hdr.sample_rate;
    out->channels = (uint8_t)hdr.channels;
    out->audio_start = (uint32_t)framePos;

    // Xing/Info sits inside the first frame: frame count and stream size make
    // the duration exact for VBR files.
    uint32_t frames = 0;
    uint32_t streamBytes = 0;
    size_t off = (size_t)(framePos - windowBase);
    size_t scanEnd = off + (size_t)hdr.frame_size;
    if (scanEnd > windowLen)
        scanEnd = windowLen;
    for (size_t i = off + 4; i + 12 <= scanEnd; i++) {
        if (memcmp(buf + i, "Xing", 4) == 0 || memcmp(buf + i, "Info", 4) == 0) {
            uint32_t xflags = read_be32(buf + i + 4);
            if (xflags & 1)
                frames = read_be32(buf + i + 8);
            if ((xflags & 2) && i + 16 <= scanEnd)
                streamBytes = read_be32(buf + i + 12);
            break;
        }
    }

    if (frames > 0 && hdr.sample_rate > 0) {
        out->duration_ms = (uint32_t)(((uint64_t)frames * (uint64_t)hdr.samples_per_frame * 1000ull) /
                                      (uint64_t)hdr.sample_rate);
    } else if (hdr.bitrate > 0 && audioBytes > 0) {
        out->duration_ms = (uint32_t)(((uint64_t)audioBytes * 8ull) / (uint64_t)hdr.bitrate);
    }

    // VBR files report the first frame's rate; the stream average is far more
    // useful in the UI.
    if (streamBytes > 0 && out->duration_ms > 0) {
        uint64_t average = ((uint64_t)streamBytes * 8ull) / (uint64_t)out->duration_ms;
        if (average > 0 && average < 1000)
            out->bitrate = (uint16_t)average;
    }

    free(buf);
    fclose(fp);
}
