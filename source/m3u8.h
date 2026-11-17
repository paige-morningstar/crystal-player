#pragma once

#include <stdbool.h>
#include <stddef.h>

// Called once per playable entry in the playlist. `resolved_path` is an
// absolute path (sdmc:/...) and `ext_title` is the #EXTINF display title, or
// NULL when the playlist does not provide one.
typedef void (*m3u8_entry_fn)(void *user, const char *resolved_path, const char *ext_title);

// Parses an extended M3U (.m3u8) or plain M3U playlist. Relative entries are
// resolved against the playlist's own folder. Returns the number of entries.
int m3u8_parse(const char *playlist_path, m3u8_entry_fn fn, void *user);

bool m3u8_is_playlist(const char *filename);
