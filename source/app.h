#pragma once

#include <3ds.h>
#include <stdbool.h>

#include "library.h"

#define APP_NAME   "Crystal Player"
#define MAX_QUEUE  4000
#define LIST_ROWS  6

typedef enum {
    VIEW_ALBUMS = 0,
    VIEW_ALBUM,
    VIEW_PLAYLISTS,
    VIEW_PLAYLIST,
} ViewId;

typedef struct {
    ViewId view;
    int    open_album;
    int    open_playlist;
    int    cursor;
    int    scroll;

    int    queue[MAX_QUEUE];
    int    queue_count;
    int    queue_pos;
    char   queue_name[128];

    bool   loaded;
    int    current;        // library index of the loaded track, -1 when idle

    char   status[160];
    u64    status_until;
} App;

extern App g_app;

// Implemented in main.c, shared with the renderer.
int         app_view_count(void);
int         app_view_track(int row);          // library index for a row
const char *app_view_title(void);
const Track *app_current_track(void);
void        app_seek_by(int32_t delta_ms);
