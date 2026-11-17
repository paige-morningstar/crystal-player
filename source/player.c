#include "player.h"

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "dr_mp3.h"

#include "util.h"

// The DSP natively runs at SYSCLOCK_SOC/512 ~= 32728 Hz. Feeding it anything
// higher glitches, so we resample the decoded stream to that rate instead.
#define OUT_RATE    32728.0f
#define CHANNEL     0
#define NUM_BUFS    4
#define BUF_FRAMES  4096       // ~125 ms per buffer
#define STG_FRAMES  4096       // decoded source frames staged between resamples
#define READER_CAP  32768

typedef struct {
    FILE *fp;
    unsigned char *data;
    size_t cap;
    size_t pos;      // consumed within data[]
    size_t len;      // valid bytes in data[]
    long long base;  // file offset of data[0]
    long long size;  // total file size
} Reader;

typedef struct {
    // decoder
    drmp3 mp3;
    bool mp3_open;
    Reader reader;
    FILE *fp;
    long long file_size;

    // thread plumbing
    Thread thread;
    volatile bool running;
    volatile bool available;

    // state
    volatile bool has_track;
    volatile bool playing;
    volatile bool paused;
    volatile bool eof;
    volatile bool finished;

    volatile bool open_pending;
    char open_path[512];
    uint32_t open_duration;
    uint32_t open_audio_start;
    uint16_t open_bitrate;
    uint32_t audio_start;
    uint16_t bitrate;

    volatile bool seek_pending;
    volatile uint32_t seek_ms;

    // output buffers
    ndspWaveBuf waves[NUM_BUFS];
    s16 *pcm;

    // resampler
    float *stg;
    size_t stg_pos;
    size_t stg_len;
    double step;
    double frac;
    int channels;
    bool src_eof;

    uint32_t duration_ms;

    // position clock (main thread only)
    volatile uint32_t base_ms;
    volatile u64 resume_tick;
} Player;

static Player P;
static LightLock s_lock;
static float s_volume = 1.0f;

// ---------------------------------------------------------------------------
// buffered file reader (dr_mp3 callbacks)
// ---------------------------------------------------------------------------

static bool reader_fill(Reader *r)
{
    if (r->pos < r->len)
        return true;

    r->base += (long long)r->len;
    r->pos = 0;
    r->len = 0;
    if (r->base >= r->size)
        return false;
    if (fseek(r->fp, (long)r->base, SEEK_SET) != 0)
        return false;
    r->len = fread(r->data, 1, r->cap, r->fp);
    return r->len > 0;
}

static size_t reader_read(void *user, void *out, size_t bytes)
{
    Reader *r = (Reader *)user;
    size_t done = 0;
    unsigned char *o = (unsigned char *)out;

    while (done < bytes) {
        if (r->pos >= r->len && !reader_fill(r))
            break;
        size_t n = r->len - r->pos;
        if (n > bytes - done)
            n = bytes - done;
        memcpy(o + done, r->data + r->pos, n);
        r->pos += n;
        done += n;
    }
    return done;
}

static void reader_jump(Reader *r, long long target)
{
    if (target < 0)
        target = 0;
    if (target > r->size)
        target = r->size;
    if (target >= r->base && target <= r->base + (long long)r->len) {
        r->pos = (size_t)(target - r->base);
    } else {
        r->base = target;
        r->pos = 0;
        r->len = 0;
    }
}

static drmp3_bool32 reader_seek(void *user, int offset, drmp3_seek_origin origin)
{
    Reader *r = (Reader *)user;
    long long target;
    if (origin == DRMP3_SEEK_SET)
        target = offset;
    else if (origin == DRMP3_SEEK_CUR)
        target = r->base + (long long)r->pos + offset;
    else
        target = r->size + offset;
    reader_jump(r, target);
    return DRMP3_TRUE;
}

static drmp3_bool32 reader_tell(void *user, drmp3_int64 *cursor)
{
    Reader *r = (Reader *)user;
    if (cursor)
        *cursor = (drmp3_int64)(r->base + (long long)r->pos);
    return DRMP3_TRUE;
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

static void reset_buffers(void)
{
    ndspChnWaveBufClear(CHANNEL);
    for (int i = 0; i < NUM_BUFS; i++) {
        P.waves[i].status = NDSP_WBUF_FREE;
        P.waves[i].nsamples = 0;
    }
}

static void close_decoder(void)
{
    if (P.mp3_open) {
        drmp3_uninit(&P.mp3);
        P.mp3_open = false;
    }
    if (P.fp) {
        fclose(P.fp);
        P.fp = NULL;
    }
}

static void reset_stream_state(void)
{
    P.stg_pos = 0;
    P.stg_len = 0;
    P.frac = 0.0;
    P.src_eof = false;
}

// Opens `path` for decoding, starting at `byte_offset` (0 for the beginning).
static bool open_decoder(const char *path, uint32_t byte_offset)
{
    close_decoder();

    P.fp = fopen(path, "rb");
    if (!P.fp)
        return false;

    if (fseek(P.fp, 0, SEEK_END) != 0) {
        close_decoder();
        return false;
    }
    long size = ftell(P.fp);
    if (size <= 0) {
        close_decoder();
        return false;
    }
    P.file_size = size;

    P.reader.fp = P.fp;
    P.reader.size = size;
    P.reader.base = 0;
    P.reader.pos = 0;
    P.reader.len = 0;

    if (byte_offset > 0 && (long long)byte_offset < size)
        reader_jump(&P.reader, byte_offset);

    if (!drmp3_init(&P.mp3, reader_read, reader_seek, reader_tell, NULL, &P.reader, NULL)) {
        close_decoder();
        return false;
    }
    P.mp3_open = true;

    P.channels = (P.mp3.channels == 1) ? 1 : 2;
    P.step = (double)P.mp3.sampleRate / (double)OUT_RATE;
    if (P.step <= 0.0 || P.step > 8.0)
        P.step = 1.0;
    reset_stream_state();
    return true;
}

static inline s16 to_s16(float v)
{
    if (v > 1.0f)
        v = 1.0f;
    else if (v < -1.0f)
        v = -1.0f;
    return (s16)(v * 32767.0f);
}

// Fills up to `max_frames` stereo frames, resampling to OUT_RATE.
static int decode_fill(s16 *out, int max_frames)
{
    int produced = 0;
    const int ch = P.channels;

    while (produced < max_frames) {
        if (P.stg_pos + 1 >= P.stg_len) {
            if (P.src_eof)
                break;

            size_t keep = P.stg_len - P.stg_pos; // 0 or 1 frame of carry
            if (keep > 1)
                keep = 1;
            if (keep == 1)
                memmove(P.stg, P.stg + P.stg_pos * (size_t)ch, sizeof(float) * (size_t)ch);

            size_t got = (size_t)drmp3_read_pcm_frames_f32(&P.mp3, STG_FRAMES - keep,
                                                          P.stg + keep * (size_t)ch);
            P.stg_pos = 0;
            P.stg_len = keep + got;
            if (got == 0) {
                P.src_eof = true;
                if (P.stg_len < 2)
                    break;
            }
            continue;
        }

        float f = (float)P.frac;
        const float *a = P.stg + P.stg_pos * (size_t)ch;
        const float *b = a + ch;
        float l = a[0] + (b[0] - a[0]) * f;
        float r = (ch > 1) ? (a[1] + (b[1] - a[1]) * f) : l;

        out[produced * 2] = to_s16(l);
        out[produced * 2 + 1] = to_s16(r);
        produced++;

        P.frac += P.step;
        while (P.frac >= 1.0) {
            P.frac -= 1.0;
            P.stg_pos++;
        }
    }

    return produced;
}

static void do_open(void)
{
    P.open_pending = false;
    P.has_track = false;
    P.playing = false;

    reset_buffers();

    if (!open_decoder(P.open_path, 0)) {
        P.finished = true;
        close_decoder();
        return;
    }

    P.duration_ms = P.open_duration;
    P.audio_start = P.open_audio_start;
    P.bitrate = P.open_bitrate;
    P.base_ms = 0;
    P.resume_tick = osGetTime();
    P.paused = false;
    P.eof = false;
    P.finished = false;
    P.has_track = true;
    P.playing = true;
    ndspChnSetPaused(CHANNEL, false);
}

static void do_seek(uint32_t target)
{
    P.seek_pending = false;
    if (!P.has_track)
        return;

    reset_buffers();
    reset_stream_state();
    P.eof = false;
    P.finished = false;

    bool ok = false;

    // Fast path: jump to an estimated byte offset and let the decoder resync.
    if (P.bitrate > 0 && target > 0) {
        uint64_t bytes_per_sec = (uint64_t)P.bitrate * 1000ull / 8ull;
        uint64_t off = (uint64_t)P.audio_start + bytes_per_sec * target / 1000ull;
        if (P.file_size > 8192 && off > (uint64_t)P.file_size - 4096ull)
            off = (uint64_t)P.file_size - 4096ull;
        ok = open_decoder(P.open_path, (uint32_t)off);
    }

    // Slow but exact fallback: rewind and decode forward.
    if (!ok) {
        ok = open_decoder(P.open_path, 0);
        if (ok) {
            uint64_t frame = (uint64_t)((double)target * (double)P.mp3.sampleRate / 1000.0);
            if (!drmp3_seek_to_pcm_frame(&P.mp3, frame))
                ok = false;
            reset_stream_state();
        }
    }

    if (!ok) {
        P.finished = true;
        P.playing = false;
        P.has_track = false;
        return;
    }

    P.base_ms = target;
    P.resume_tick = osGetTime();
    P.has_track = true;
    P.playing = true;
    ndspChnSetPaused(CHANNEL, P.paused);
}

static void audio_thread(void *arg)
{
    (void)arg;

    while (P.running) {
        if (P.open_pending) {
            __sync_synchronize();
            do_open();
            continue;
        }
        if (P.seek_pending) {
            do_seek(P.seek_ms);
            continue;
        }
        if (!P.has_track || P.paused) {
            svcSleepThread(8000000LL);
            continue;
        }
        if (P.eof) {
            bool busy = false;
            for (int i = 0; i < NUM_BUFS; i++) {
                if (P.waves[i].status == NDSP_WBUF_QUEUED || P.waves[i].status == NDSP_WBUF_PLAYING)
                    busy = true;
            }
            if (!busy) {
                P.finished = true;
                P.playing = false;
                P.has_track = false;
            }
            svcSleepThread(20000000LL);
            continue;
        }

        int idx = -1;
        for (int i = 0; i < NUM_BUFS; i++) {
            if (P.waves[i].status == NDSP_WBUF_DONE || P.waves[i].status == NDSP_WBUF_FREE) {
                idx = i;
                break;
            }
        }
        if (idx < 0) {
            svcSleepThread(4000000LL);
            continue;
        }

        int frames = decode_fill(P.pcm + (size_t)idx * BUF_FRAMES * 2, BUF_FRAMES);
        if (frames <= 0) {
            P.eof = true;
            continue;
        }

        P.waves[idx].nsamples = (u32)frames;
        DSP_FlushDataCache(P.waves[idx].data_vaddr, (u32)(frames * 4));
        ndspChnWaveBufAdd(CHANNEL, &P.waves[idx]);
    }

    threadExit(0);
}

// ---------------------------------------------------------------------------
// public API
// ---------------------------------------------------------------------------

bool player_init(void)
{
    memset(&P, 0, sizeof(P));
    LightLock_Init(&s_lock);

    if (R_FAILED(ndspInit()))
        return false;

    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnSetInterp(CHANNEL, NDSP_INTERP_LINEAR);
    ndspChnSetRate(CHANNEL, OUT_RATE);
    ndspChnSetFormat(CHANNEL, NDSP_FORMAT_STEREO_PCM16);

    float mix[12];
    memset(mix, 0, sizeof(mix));
    mix[0] = s_volume;
    mix[1] = s_volume;
    ndspChnSetMix(CHANNEL, mix);

    P.pcm = (s16 *)linearAlloc((size_t)NUM_BUFS * BUF_FRAMES * 2 * sizeof(s16));
    P.stg = (float *)malloc((size_t)STG_FRAMES * 2 * sizeof(float));
    P.reader.cap = READER_CAP;
    P.reader.data = (unsigned char *)malloc(READER_CAP);

    if (!P.pcm || !P.stg || !P.reader.data) {
        if (P.pcm)
            linearFree(P.pcm);
        free(P.stg);
        free(P.reader.data);
        ndspExit();
        memset(&P, 0, sizeof(P));
        return false;
    }

    for (int i = 0; i < NUM_BUFS; i++) {
        P.waves[i].data_vaddr = P.pcm + (size_t)i * BUF_FRAMES * 2;
        P.waves[i].nsamples = 0;
        P.waves[i].status = NDSP_WBUF_FREE;
        P.waves[i].looping = false;
    }

    P.available = true;
    P.running = true;
    P.thread = threadCreate(audio_thread, NULL, 32 * 1024, 0x2F, -1, false);
    if (!P.thread) {
        P.running = false;
        P.available = false;
        return false;
    }

    return true;
}

void player_shutdown(void)
{
    if (!P.available)
        return;

    P.running = false;
    P.available = false;
    threadJoin(P.thread, U64_MAX);
    threadFree(P.thread);

    reset_buffers();
    close_decoder();

    ndspChnWaveBufClear(CHANNEL);
    ndspExit();

    linearFree(P.pcm);
    free(P.stg);
    free(P.reader.data);

    P.pcm = NULL;
    P.stg = NULL;
    P.reader.data = NULL;
    P.has_track = false;
}

void player_play(const PlayerTrack *track)
{
    if (!track || !track->path || !P.available)
        return;

    LightLock_Lock(&s_lock);
    util_copy(P.open_path, sizeof(P.open_path), track->path);
    P.open_duration = track->duration_ms;
    P.open_audio_start = track->audio_start;
    P.open_bitrate = track->bitrate;
    P.seek_pending = false;
    P.paused = false;
    P.finished = false;
    P.base_ms = 0;
    P.resume_tick = osGetTime();
    __sync_synchronize();
    P.open_pending = true;
    LightLock_Unlock(&s_lock);
}

void player_toggle_pause(void)
{
    if (!P.has_track)
        return;

    if (!P.paused) {
        P.base_ms = player_pos_ms();
        P.paused = true;
        ndspChnSetPaused(CHANNEL, true);
    } else {
        P.paused = false;
        P.resume_tick = osGetTime();
        ndspChnSetPaused(CHANNEL, false);
    }
}

bool player_paused(void)
{
    return P.paused;
}

bool player_has_track(void)
{
    return P.has_track;
}

bool player_take_finished(void)
{
    if (!P.finished)
        return false;
    P.finished = false;
    return true;
}

void player_seek_to(uint32_t ms)
{
    if (!P.has_track)
        return;

    uint32_t dur = P.duration_ms;
    if (dur && ms > dur)
        ms = dur;

    P.base_ms = ms;
    P.resume_tick = osGetTime();
    P.seek_ms = ms;
    P.seek_pending = true;
}

void player_seek_by(int32_t delta_ms)
{
    if (!P.has_track)
        return;

    int64_t target = (int64_t)player_pos_ms() + delta_ms;
    if (target < 0)
        target = 0;
    player_seek_to((uint32_t)target);
}

uint32_t player_pos_ms(void)
{
    if (!P.has_track)
        return 0;

    uint32_t base = P.base_ms;
    if (P.playing && !P.paused) {
        u64 now = osGetTime();
        if (now > P.resume_tick)
            base += (uint32_t)(now - P.resume_tick);
    }
    if (P.duration_ms && base > P.duration_ms)
        base = P.duration_ms;
    return base;
}

uint32_t player_duration_ms(void)
{
    return P.duration_ms;
}

void player_set_volume(float v)
{
    if (v < 0.0f)
        v = 0.0f;
    if (v > 1.0f)
        v = 1.0f;
    s_volume = v;

    float mix[12];
    memset(mix, 0, sizeof(mix));
    mix[0] = v;
    mix[1] = v;
    ndspChnSetMix(CHANNEL, mix);
}

float player_volume(void)
{
    return s_volume;
}
