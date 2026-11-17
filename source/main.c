#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "art.h"
#include "library.h"
#include "player.h"
#include "ui.h"
#include "util.h"

App g_app;

static bool s_quit;

const Track *app_current_track(void)
{
    if (g_app.current < 0 || g_app.current >= g_lib.track_count)
        return NULL;
    return &g_lib.tracks[g_app.current];
}

// The album or playlist the list is showing, NULL when a rescan removed it.
static Album *current_album(void)
{
    if (g_app.open_album < 0 || g_app.open_album >= g_lib.album_count)
        return NULL;
    return &g_lib.albums[g_app.open_album];
}

static Playlist *current_playlist(void)
{
    if (g_app.open_playlist < 0 || g_app.open_playlist >= g_lib.playlist_count)
        return NULL;
    return &g_lib.playlists[g_app.open_playlist];
}

int app_view_count(void)
{
    switch (g_app.view) {
    case VIEW_ALBUMS:
        return g_lib.album_count;
    case VIEW_PLAYLISTS:
        return g_lib.playlist_count;
    case VIEW_ALBUM: {
        Album *album = current_album();
        return album ? album->count : 0;
    }
    case VIEW_PLAYLIST:
    default: {
        Playlist *pl = current_playlist();
        return pl ? pl->count : 0;
    }
    }
}

int app_view_track(int row)
{
    if (row < 0)
        return -1;

    Album *album = (g_app.view == VIEW_ALBUM) ? current_album() : NULL;
    if (album)
        return row < album->count ? album->tracks[row] : -1;

    Playlist *pl = (g_app.view == VIEW_PLAYLIST) ? current_playlist() : NULL;
    if (pl)
        return row < pl->count ? pl->tracks[row] : -1;

    return -1;
}

const char *app_view_title(void)
{
    switch (g_app.view) {
    case VIEW_ALBUMS:
        return "Albums";
    case VIEW_PLAYLISTS:
        return "Playlists";
    case VIEW_ALBUM: {
        Album *album = current_album();
        return album ? album->name : "Album";
    }
    case VIEW_PLAYLIST:
    default: {
        Playlist *pl = current_playlist();
        return pl ? pl->name : "Playlist";
    }
    }
}

static void play_queue_slot(void)
{
    if (g_app.queue_pos < 0 || g_app.queue_pos >= g_app.queue_count)
        return;
    int idx = g_app.queue[g_app.queue_pos];
    if (idx < 0 || idx >= g_lib.track_count)
        return;

    const Track *t = &g_lib.tracks[idx];
    PlayerTrack pt;
    pt.path = t->path;
    pt.duration_ms = t->duration_ms;
    pt.audio_start = t->audio_start;
    pt.bitrate = t->bitrate;
    player_play(&pt);

    g_app.current = idx;
    g_app.loaded = true;
}

// Randomises the songs that have not played yet, leaving the current one alone.
static void shuffle_rest(uint32_t seed)
{
    util_shuffle_range(g_app.queue + g_app.queue_pos + 1,
                       g_app.queue_count - g_app.queue_pos - 1, seed);
}

// Back to the top of the queue. The shuffle covers every slot but the first,
// so the song that just ended - still sitting in the last slot - cannot be moved
// to the top and play twice in a row.
static void wrap_queue(void)
{
    if (g_app.shuffle && g_app.queue_count > 1)
        util_shuffle_range(g_app.queue + 1, g_app.queue_count - 1, (uint32_t)osGetTime());
    g_app.queue_pos = 0;
    play_queue_slot();
}

// Fills the queue from the current view and starts playing the chosen row.
static void play_from_row(int row)
{
    int count = app_view_count();
    if (row < 0 || row >= count)
        return;

    int n = 0;
    for (int i = 0; i < count && n < MAX_QUEUE; i++) {
        int idx = app_view_track(i);
        if (idx >= 0)
            g_app.queue[n++] = idx;
    }
    if (n == 0)
        return;

    g_app.queue_count = n;
    g_app.queue_pos = row;
    if (g_app.queue_pos >= g_app.queue_count)
        g_app.queue_pos = g_app.queue_count - 1;

    Album *album = (g_app.view == VIEW_ALBUM) ? current_album() : NULL;
    Playlist *pl = (g_app.view == VIEW_PLAYLIST) ? current_playlist() : NULL;
    if (album)
        util_copy(g_app.queue_name, sizeof(g_app.queue_name), album->name);
    else if (pl)
        util_copy(g_app.queue_name, sizeof(g_app.queue_name), pl->name);

    if (g_app.shuffle)
        shuffle_rest((uint32_t)osGetTime());

    play_queue_slot();
}

// Fills the queue with one album, positioned on `track` when it is a member.
static void queue_album(int album_index, int track)
{
    const Album *album = &g_lib.albums[album_index];
    int count = album->count < MAX_QUEUE ? album->count : MAX_QUEUE;

    for (int i = 0; i < count; i++) {
        g_app.queue[i] = album->tracks[i];
        if (album->tracks[i] == track)
            g_app.queue_pos = i;
    }
    g_app.queue_count = count;
    util_copy(g_app.queue_name, sizeof(g_app.queue_name), album->name);
}

static void next_track(void)
{
    if (g_app.queue_count == 0)
        return;
    if (g_app.queue_pos + 1 < g_app.queue_count) {
        g_app.queue_pos++;
        play_queue_slot();
        return;
    }
    // Past the last song: repeat all goes round, repeat one does nothing more.
    if (g_app.repeat == 1)
        wrap_queue();
}

static void prev_track(void)
{
    if (g_app.queue_count == 0)
        return;
    // Standard player behaviour: restart the song unless we are near its start.
    if (player_has_track() && player_pos_ms() > 3000) {
        player_seek_to(0);
        return;
    }
    if (g_app.queue_pos > 0) {
        g_app.queue_pos--;
        play_queue_slot();
    } else {
        player_seek_to(0);
    }
}

void app_seek_by(int32_t delta_ms)
{
    if (player_has_track())
        player_seek_by(delta_ms);
}

static void advance_after_finish(void)
{
    if (g_app.queue_count == 0)
        return;

    if (g_app.repeat == 2) { // repeat one: start the same song again
        play_queue_slot();
        return;
    }

    if (g_app.queue_pos + 1 < g_app.queue_count) {
        g_app.queue_pos++;
        play_queue_slot();
        return;
    }

    if (g_app.repeat == 1) { // repeat all: back to the top of the queue
        wrap_queue();
        return;
    }

    g_app.current = -1;
    g_app.loaded = false;
    ui_message("End of %s", g_app.queue_name[0] ? g_app.queue_name : "queue");
}

static void switch_view(ViewId view)
{
    g_app.view = view;
    g_app.cursor = 0;
    g_app.scroll = 0;
    g_app.open_album = -1;
    g_app.open_playlist = -1;
}

// Returns to a list view and puts the cursor back on `cursor` when it still exists.
static void back_to(ViewId view, int cursor)
{
    switch_view(view);
    int count = app_view_count();
    if (cursor >= 0 && cursor < count)
        g_app.cursor = cursor;
}

// Re-scans the SD card. The playing song keeps playing; the queue follows the
// new library order so skip/next still make sense.
static void rescan_library(void)
{
    char playing_path[512];
    playing_path[0] = 0;
    const Track *t = app_current_track();
    if (t)
        util_copy(playing_path, sizeof(playing_path), t->path);

    ViewId view = g_app.view;
    int cursor = g_app.cursor;
    int found = library_scan(ui_scan_progress);

    // Album and playlist indices are rebuilt by the scan, so fall back to the
    // list that still exists.
    if (view == VIEW_PLAYLIST)
        view = VIEW_PLAYLISTS;
    else if (view == VIEW_ALBUM)
        view = VIEW_ALBUMS;
    back_to(view, cursor);

    // Exact only: a same-named song from another album is not the one playing.
    g_app.current = playing_path[0] ? library_find_exact(playing_path) : -1;
    g_app.loaded = false;
    g_app.queue_count = 0;
    g_app.queue_pos = 0;

    int album_index = library_album_of_track(g_app.current);
    if (g_app.current >= 0 && album_index >= 0) {
        queue_album(album_index, g_app.current);
        g_app.loaded = true;
    }

    ui_message("%d songs found", found);
}

static void volume_step(int dir)
{
    float v = player_volume() + (dir > 0 ? 0.0625f : -0.0625f);
    if (v < 0.0f)
        v = 0.0f;
    if (v > 1.0f)
        v = 1.0f;
    player_set_volume(v);
    ui_message("Volume %d%%", (int)(v * 100.0f + 0.5f));
}

static void toggle_shuffle(void)
{
    g_app.shuffle = !g_app.shuffle;
    if (g_app.shuffle)
        shuffle_rest((uint32_t)osGetTime());
    ui_message("Shuffle %s", g_app.shuffle ? "on" : "off");
}

static void cycle_repeat(void)
{
    g_app.repeat = (g_app.repeat + 1) % 3;
    ui_message("Repeat %s",
               g_app.repeat == 0 ? "off" : (g_app.repeat == 1 ? "all" : "one"));
}

static void handle_input(void)
{
    u32 down = hidKeysDown();
    u32 held = hidKeysHeld();
    u32 up = hidKeysUp();
    static int seek_hold;
    static bool select_used;

    if (down & KEY_START) {
        s_quit = true;
        return;
    }

    // SELECT is a modifier while it is held: + d-pad up/down sets the volume,
    // + Y toggles shuffle, + X cycles repeat. A plain tap still switches between
    // albums and playlists, on release, so a hold never fires that by accident.
    if (down & KEY_SELECT)
        select_used = false;

    if (held & KEY_SELECT) {
        if (down & KEY_UP) {
            volume_step(+1);
            select_used = true;
        }
        if (down & KEY_DOWN) {
            volume_step(-1);
            select_used = true;
        }
        if (down & KEY_Y) {
            toggle_shuffle();
            select_used = true;
        }
        if (down & KEY_X) {
            cycle_repeat();
            select_used = true;
        }
        if (select_used)
            return;
    } else if (up & KEY_SELECT) {
        if (!select_used)
            switch_view(g_app.view == VIEW_PLAYLISTS ? VIEW_ALBUMS : VIEW_PLAYLISTS);
        return;
    }

    if (down & KEY_X)
        player_toggle_pause();
    if (down & KEY_ZR)
        next_track();
    if (down & KEY_ZL)
        prev_track();

    if (down & (KEY_LEFT | KEY_RIGHT)) {
        app_seek_by((down & KEY_RIGHT) ? 5000 : -5000);
        seek_hold = 0;
    } else if (held & (KEY_LEFT | KEY_RIGHT)) {
        if (++seek_hold >= 8) {
            seek_hold = 0;
            app_seek_by((held & KEY_RIGHT) ? 2000 : -2000);
        }
    } else {
        seek_hold = 0;
    }

    if (down & KEY_B) {
        if (g_app.view == VIEW_PLAYLIST)
            back_to(VIEW_PLAYLISTS, g_app.open_playlist);
        else if (g_app.view == VIEW_ALBUM)
            back_to(VIEW_ALBUMS, g_app.open_album);
        else if (g_app.view == VIEW_PLAYLISTS)
            back_to(VIEW_ALBUMS, g_app.cursor);
        return;
    }

    if (down & KEY_Y) {
        rescan_library();
        return;
    }

    int count = app_view_count();
    if (count <= 0)
        return;

    if (down & KEY_DOWN) {
        if (g_app.cursor + 1 < count)
            g_app.cursor++;
    }
    if (down & KEY_UP) {
        if (g_app.cursor > 0)
            g_app.cursor--;
    }
    if (down & KEY_R) {
        g_app.cursor += LIST_ROWS;
        if (g_app.cursor >= count)
            g_app.cursor = count - 1;
    }
    if (down & KEY_L) {
        g_app.cursor -= LIST_ROWS;
        if (g_app.cursor < 0)
            g_app.cursor = 0;
    }

    if (down & KEY_A) {
        if (g_app.view == VIEW_ALBUMS) {
            if (g_app.cursor < g_lib.album_count) {
                int open = g_app.cursor;
                switch_view(VIEW_ALBUM);
                g_app.open_album = open;
            }
        } else if (g_app.view == VIEW_PLAYLISTS) {
            if (g_app.cursor < g_lib.playlist_count) {
                int open = g_app.cursor;
                switch_view(VIEW_PLAYLIST);
                g_app.open_playlist = open;
            }
        } else {
            play_from_row(g_app.cursor);
        }
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    osSetSpeedupEnable(true); // 804 MHz on New 3DS, no-op elsewhere
    gfxInitDefault();
    cfguInit(); // required by the shared system font used for text

    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    ui_init();
    art_init();

    // Keep the DSP alive when the lid is closed so music keeps playing. libctru
    // brings APT up before main(), so this is not lost to a late aptInit().
    aptSetSleepAllowed(false);

    bool audio_ok = player_init();

    g_app.view = VIEW_ALBUMS;
    g_app.current = -1;
    g_app.open_album = -1;
    g_app.open_playlist = -1;

    ui_scan_progress(0, MUSIC_ROOT);
    int tracks = library_scan(ui_scan_progress);

    if (!audio_ok)
        ui_message("Audio could not start");
    else if (tracks == 0)
        ui_message("No MP3s in sdmc:/music");
    else
        ui_message("%d songs found", tracks);

    while (aptMainLoop() && !s_quit) {
        hidScanInput();
        handle_input();
        if (s_quit)
            break;
        if (player_take_finished())
            advance_after_finish();
        ui_draw();
    }

    player_shutdown();
    art_shutdown();
    ui_shutdown();
    library_free();

    C2D_Fini();
    C3D_Fini();
    cfguExit();
    gfxExit();
    return 0;
}
