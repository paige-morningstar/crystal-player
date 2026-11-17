#include "ui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "art.h"
#include "player.h"
#include "util.h"

#define COL_BG    C2D_Color32(0x0E, 0x12, 0x19, 0xFF)
#define COL_BAR   C2D_Color32(0x1A, 0x20, 0x2D, 0xFF)
#define COL_PANEL C2D_Color32(0x17, 0x1D, 0x29, 0xFF)
#define COL_ROW   C2D_Color32(0x23, 0x2C, 0x3E, 0xFF)
#define COL_TXT   C2D_Color32(0xEC, 0xF1, 0xF8, 0xFF)
#define COL_DIM   C2D_Color32(0x87, 0x93, 0xA5, 0xFF)
#define COL_ACC   C2D_Color32(0x4F, 0xC3, 0xF7, 0xFF)
#define COL_ACC2  C2D_Color32(0x8A, 0xE0, 0xA8, 0xFF)
#define COL_TRACK C2D_Color32(0x2B, 0x33, 0x44, 0xFF)

#define ROW_HEIGHT 32
#define LIST_TOP 28.0f

static C3D_RenderTarget *s_top;
static C3D_RenderTarget *s_bottom;
static C2D_TextBuf s_text;
static C2D_TextBuf s_measure;
static C2D_Image *s_cover;

static void rect(float x, float y, float w, float h, u32 color)
{
    if (w <= 0.0f || h <= 0.0f)
        return;
    C2D_DrawRectSolid(x, y, 0.5f, w, h, color);
}

static void circle(float x, float y, float r, u32 color)
{
    C2D_DrawCircleSolid(x, y, 0.5f, r, color);
}

static void text_draw(const char *s, float x, float y, float scale, u32 color)
{
    if (!s || !s[0])
        return;
    C2D_Text text;
    C2D_TextParse(&text, s_text, s);
    C2D_DrawText(&text, C2D_WithColor, x, y, 0.5f, scale, scale, color);
}

static float text_w(const char *s, float scale)
{
    if (!s || !s[0])
        return 0.0f;
    C2D_TextBufClear(s_measure);
    C2D_Text text;
    C2D_TextParse(&text, s_measure, s);
    float w = 0.0f, h = 0.0f;
    C2D_TextGetDimensions(&text, scale, scale, &w, &h);
    return w;
}

// Draws `s`, shortening it with an ellipsis until it fits into maxw pixels.
static void text_fit(const char *s, float x, float y, float maxw, float scale, u32 color)
{
    if (!s || !s[0])
        return;
    if (text_w(s, scale) <= maxw) {
        text_draw(s, x, y, scale, color);
        return;
    }

    char buf[256];
    util_copy(buf, sizeof(buf), s);
    int len = (int)strlen(buf);
    int best = 0;
    int lo = 0, hi = len;

    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        while (mid > 0 && ((unsigned char)buf[mid] & 0xC0) == 0x80)
            mid--;
        if (mid > 240)
            mid = 240;

        char tmp[264];
        memcpy(tmp, buf, (size_t)mid);
        tmp[mid] = 0;
        strcat(tmp, "...");

        if (text_w(tmp, scale) <= maxw) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }

    char out[264];
    memcpy(out, buf, (size_t)best);
    out[best] = 0;
    strcat(out, "...");
    text_draw(out, x, y, scale, color);
}

static void draw_top(void)
{
    C2D_TargetClear(s_top, COL_BG);
    C2D_SceneBegin(s_top);

    rect(0, 0, 400, 26, COL_BAR);
    text_draw(APP_NAME, 14, 5, 0.5f, COL_ACC);

    char status[192];
    status[0] = 0;
    if (g_app.status[0] && osGetTime() < g_app.status_until)
        util_copy(status, sizeof(status), g_app.status);
    else if (player_has_track())
        util_copy(status, sizeof(status), player_paused() ? "PAUSED" : "PLAYING");

    if (status[0])
        text_draw(status, 386.0f - text_w(status, 0.38f), 8, 0.38f, COL_DIM);

    const Track *t = app_current_track();

    const float ax = 18.0f, ay = 46.0f, asz = 170.0f;
    if (s_cover) {
        float sc = asz / (float)ART_PIXELS;
        C2D_DrawImageAt(*s_cover, ax, ay, 0.5f, NULL, sc, sc);
    } else {
        rect(ax, ay, asz, asz, COL_PANEL);
        rect(ax, ay, asz, 1.0f, COL_TRACK);
        rect(ax, ay + asz - 1.0f, asz, 1.0f, COL_TRACK);
        if (t)
            text_draw("no cover art", ax + 50.0f, ay + asz / 2.0f - 5.0f, 0.36f, COL_DIM);
    }

    const float cx = 206.0f, cw = 176.0f;

    if (!t) {
        text_draw("NOTHING PLAYING", cx, 60.0f, 0.4f, COL_DIM);
        text_draw("Choose a song on the", cx, 88.0f, 0.44f, COL_TXT);
        text_draw("lower screen and", cx, 106.0f, 0.44f, COL_TXT);
        text_draw("press A.", cx, 124.0f, 0.44f, COL_TXT);
    } else {
        text_draw("NOW PLAYING", cx, 44.0f, 0.32f, COL_DIM);
        text_fit(t->title, cx, 58.0f, cw, 0.56f, COL_TXT);
        text_fit(t->artist, cx, 88.0f, cw, 0.44f, COL_ACC);

        char album[220];
        if (t->year)
            snprintf(album, sizeof(album), "%s - %u", t->album, (unsigned)t->year);
        else
            snprintf(album, sizeof(album), "%s", t->album);
        text_fit(album, cx, 110.0f, cw, 0.38f, COL_DIM);

        uint32_t pos = player_pos_ms();
        uint32_t dur = t->duration_ms ? t->duration_ms : player_duration_ms();
        char elapsed[16], total[16];
        util_format_time(pos, elapsed, sizeof(elapsed));
        util_format_time(dur, total, sizeof(total));

        const float bar_y = 154.0f;
        text_draw(elapsed, cx, bar_y - 20.0f, 0.4f, COL_TXT);
        text_draw(total, cx + cw - text_w(total, 0.4f), bar_y - 20.0f, 0.4f, COL_DIM);

        rect(cx, bar_y, cw, 6.0f, COL_TRACK);
        float p = dur ? (float)pos / (float)dur : 0.0f;
        if (p < 0.0f)
            p = 0.0f;
        if (p > 1.0f)
            p = 1.0f;
        rect(cx, bar_y, cw * p, 6.0f, COL_ACC);
        circle(cx + cw * p, bar_y + 3.0f, 4.5f, COL_TXT);

        char info[96];
        snprintf(info, sizeof(info), "%u kbps   %u Hz   %s", (unsigned)t->bitrate,
                 (unsigned)t->sample_rate, t->channels == 1 ? "mono" : "stereo");
        text_draw(info, cx, 170.0f, 0.32f, COL_DIM);

        if (g_app.queue_pos + 1 < g_app.queue_count) {
            int ni = g_app.queue[g_app.queue_pos + 1];
            if (ni >= 0 && ni < g_lib.track_count) {
                text_draw("NEXT", cx, 192.0f, 0.3f, COL_DIM);
                text_fit(g_lib.tracks[ni].title, cx, 204.0f, cw, 0.38f, COL_DIM);
            }
        } else {
            text_draw("last track in queue", cx, 200.0f, 0.32f, COL_DIM);
        }
    }

    rect(0, 226, 400, 14, COL_BAR);
    char q[200];
    snprintf(q, sizeof(q), "%s   %d / %d", g_app.queue_name[0] ? g_app.queue_name : "no queue",
             g_app.queue_count ? g_app.queue_pos + 1 : 0, g_app.queue_count);
    text_draw(q, 12, 228, 0.32f, COL_DIM);
}

static void draw_bottom(void)
{
    C2D_TargetClear(s_bottom, COL_BG);
    C2D_SceneBegin(s_bottom);

    rect(0, 0, 320, 24, COL_BAR);
    text_draw(app_view_title(), 10, 4, 0.44f, COL_TXT);

    char right[96];
    if (g_app.view == VIEW_ALBUMS)
        snprintf(right, sizeof(right), "%d albums", g_lib.album_count);
    else if (g_app.view == VIEW_PLAYLISTS)
        snprintf(right, sizeof(right), "%d lists", g_lib.playlist_count);
    else
        snprintf(right, sizeof(right), "%d songs", app_view_count());
    text_draw(right, 310.0f - text_w(right, 0.32f), 7, 0.32f, COL_DIM);

    int count = app_view_count();
    const int rows = LIST_ROWS;

    if (g_app.cursor < g_app.scroll)
        g_app.scroll = g_app.cursor;
    if (g_app.cursor >= g_app.scroll + rows)
        g_app.scroll = g_app.cursor - rows + 1;
    if (g_app.scroll < 0)
        g_app.scroll = 0;

    for (int i = 0; i < rows; i++) {
        int row = g_app.scroll + i;
        if (row >= count)
            break;

        float y = LIST_TOP + (float)i * ROW_HEIGHT;
        bool selected = (row == g_app.cursor);

        if (selected) {
            rect(0, y, 320, ROW_HEIGHT - 2, COL_ROW);
            rect(0, y, 3, ROW_HEIGHT - 2, COL_ACC);
        }

        if (g_app.view == VIEW_PLAYLISTS) {
            Playlist *pl = &g_lib.playlists[row];
            text_fit(pl->name, 12, y + 2, 250, 0.46f, COL_TXT);
            char sub[128];
            if (pl->missing)
                snprintf(sub, sizeof(sub), "%d songs  -  %d missing", pl->count, pl->missing);
            else
                snprintf(sub, sizeof(sub), "%d songs", pl->count);
            text_draw(sub, 12, y + 19, 0.3f, COL_DIM);
        } else if (g_app.view == VIEW_ALBUMS) {
            const Album *al = &g_lib.albums[row];
            text_fit(al->name, 12, y + 2, 292, 0.44f, COL_TXT);
            char sub[220];
            snprintf(sub, sizeof(sub), "%s  -  %d songs", al->artist, al->count);
            text_fit(sub, 12, y + 19, 292, 0.3f, COL_DIM);
        } else {
            int idx = app_view_track(row);
            if (idx < 0 || idx >= g_lib.track_count)
                continue;
            const Track *t = &g_lib.tracks[idx];
            bool playing = (idx == g_app.current);
            if (playing)
                rect(6, y + 8, 4, ROW_HEIGHT - 16, COL_ACC2);

            float tx = 16.0f;
            float tw = 296.0f;
            if (g_app.view == VIEW_ALBUM && t->track_no) {
                char num[8];
                snprintf(num, sizeof(num), "%u", (unsigned)t->track_no);
                text_draw(num, 10, y + 8, 0.36f, COL_DIM);
                tx = 30.0f;
                tw = 282.0f;
            }

            text_fit(t->title, tx, y + 2, tw, 0.44f, playing ? COL_ACC2 : COL_TXT);

            char sub[400];
            if (g_app.view == VIEW_ALBUM)
                snprintf(sub, sizeof(sub), "%s", t->artist);
            else
                snprintf(sub, sizeof(sub), "%s  -  %s", t->artist, t->album);
            text_fit(sub, tx, y + 19, tw, 0.3f, COL_DIM);
        }
    }

    if (count > rows) {
        float track_y = LIST_TOP;
        float track_h = (float)rows * ROW_HEIGHT;
        rect(315, track_y, 3, track_h, COL_TRACK);
        float bar_h = track_h * (float)rows / (float)count;
        if (bar_h < 12.0f)
            bar_h = 12.0f;
        float bar_y = track_y + (track_h - bar_h) * (float)g_app.scroll / (float)(count - rows);
        rect(315, bar_y, 3, bar_h, COL_ACC);
    }

    if (count == 0) {
        if (g_app.view == VIEW_PLAYLISTS) {
            text_draw("No playlists found.", 16, 96, 0.44f, COL_TXT);
            text_draw("Drop a .m3u8 file into", 16, 122, 0.36f, COL_DIM);
            text_draw("sdmc:/music (or any album", 16, 140, 0.36f, COL_DIM);
            text_draw("folder) and rescan.", 16, 158, 0.36f, COL_DIM);
        } else {
            text_draw("No MP3 files found.", 16, 96, 0.44f, COL_TXT);
            text_draw("Copy albums to sdmc:/music,", 16, 122, 0.36f, COL_DIM);
            text_draw("one sub folder per album.", 16, 140, 0.36f, COL_DIM);
        }
    }

    rect(0, 222, 320, 18, COL_BAR);
    const char *hint;
    if (g_app.view == VIEW_ALBUMS)
        hint = "A open   Y rescan   SELECT playlists   START exit";
    else if (g_app.view == VIEW_PLAYLISTS)
        hint = "A open   Y rescan   SELECT albums   START exit";
    else
        hint = "A play  B back  X pause  ZL/ZR skip  </>seek";
    text_draw(hint, 8, 226, 0.29f, COL_DIM);
}

void ui_init(void)
{
    s_top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    s_bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    s_text = C2D_TextBufNew(6144);
    s_measure = C2D_TextBufNew(1024);
}

void ui_shutdown(void)
{
    if (s_text)
        C2D_TextBufDelete(s_text);
    if (s_measure)
        C2D_TextBufDelete(s_measure);
    s_text = NULL;
    s_measure = NULL;
}

void ui_draw(void)
{
    // Cover art is decoded before the frame starts so decoding never stalls a
    // half-built GPU command buffer.
    s_cover = NULL;
    const Track *t = app_current_track();
    if (t && t->art_size) {
        char key[420];
        snprintf(key, sizeof(key), "%s#%u#%u", t->folder, (unsigned)t->art_offset,
                 (unsigned)t->art_size);
        s_cover = art_get(key, t->path, t->art_offset, t->art_size);
    }

    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TextBufClear(s_text);
    draw_top();
    draw_bottom();
    C3D_FrameEnd(0);
}

void ui_scan_progress(int files, const char *name)
{
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TextBufClear(s_text);

    C2D_TargetClear(s_top, COL_BG);
    C2D_SceneBegin(s_top);
    text_draw(APP_NAME, 20, 88, 0.9f, COL_ACC);
    char line[128];
    snprintf(line, sizeof(line), "%d songs found", files);
    text_draw(line, 20, 132, 0.46f, COL_TXT);
    if (name)
        text_fit(name, 20, 158, 360, 0.36f, COL_DIM);

    C2D_TargetClear(s_bottom, COL_BG);
    C2D_SceneBegin(s_bottom);
    text_draw("Scanning sdmc:/music ...", 10, 108, 0.44f, COL_DIM);

    C3D_FrameEnd(0);
}

void ui_message(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_app.status, sizeof(g_app.status), fmt, args);
    va_end(args);
    g_app.status_until = osGetTime() + 3000;
}
