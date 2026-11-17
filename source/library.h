#pragma once

#include <stdbool.h>
#include <stdint.h>

#define MUSIC_ROOT "sdmc:/music"
#define LIB_MAX_PLAYLIST_TRACKS 4000

typedef struct {
    char     path[512];
    char     title[160];
    char     artist[160];
    char     album_artist[160];   // TPE2, "" when the file has none
    char     album[160];
    char     folder[192];   // sub folder under sdmc:/music, "" for the root
    uint16_t track_no;
    uint16_t disc_no;
    uint16_t year;
    uint32_t duration_ms;
    uint16_t bitrate;
    uint32_t sample_rate;
    uint8_t  channels;
    uint32_t art_offset;
    uint32_t art_size;
    uint32_t audio_start;  // byte offset of the first MP3 frame
} Track;

// One album: every track that shares an album title and album artist (the
// track artist when a file has no album artist tag), in disc and track order.
typedef struct {
    char  name[160];
    char  artist[160];
    int  *tracks;   // indices into Library.tracks
    int   count;
} Album;

typedef struct {
    char name[160];
    char path[512];
    int  count;
    int  missing;   // entries in the m3u8 that were not found in the library
    int *tracks;    // indices into Library.tracks
} Playlist;

typedef struct {
    Track    *tracks;
    int       track_count;
    int       track_capacity;
    Album    *albums;           // sorted by artist, then album title
    int       album_count;
    int       album_capacity;
    Playlist *playlists;
    int       playlist_count;
    int       playlist_capacity;
} Library;

extern Library g_lib;

typedef void (*library_progress_fn)(int files_seen, const char *current_file);

// Walks MUSIC_ROOT, reads tags, groups the tracks into albums and imports
// playlists. Returns the number of tracks.
int  library_scan(library_progress_fn progress);

// Regroups the scanned tracks into albums. Called by library_scan.
void library_build_albums(void);

// Finds a track by its exact path. Use this when a near miss would be wrong,
// e.g. re-finding the song that is already playing.
int  library_find_exact(const char *path);

// Finds a track by path, falling back to its file name when the path is stale
// (playlists written on another machine). Not for resuming playback: the song
// that answers to the name may not be the song that was playing.
int  library_find_path(const char *path);
int  library_album_of_track(int track);   // -1 when the track has no album entry
void library_free(void);
