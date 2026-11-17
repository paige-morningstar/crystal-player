#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Bounded string helpers used all over the player.

void util_copy(char *dst, size_t cap, const char *src);
void util_trim(char *s);
bool util_ends_with_icase(const char *s, const char *suffix);
bool util_starts_with_icase(const char *s, const char *prefix);
int  util_cmp_icase(const char *a, const char *b);

// Path helpers. All paths handled by the player use forward slashes,
// e.g. "sdmc:/music/Album/01 Song.mp3".
void util_dirname(const char *path, char *out, size_t cap);
void util_basename(const char *path, char *out, size_t cap);
void util_strip_ext(char *name);
// Resolves `entry` against `dir` (which may be ""), normalising "\\" to "/",
// collapsing "//", "." and "..". Absolute entries are kept as-is.
void util_join_normalized(char *out, size_t cap, const char *dir, const char *entry);

void util_format_time(uint32_t ms, char *out, size_t cap);

// Reorders a square RGBA8 image of `size` pixels per side into the layout the
// GPU samples: row-major 8x8 tiles, texels in Z order inside a tile, and each
// texel stored as A,B,G,R. `dst` needs size*size*4 bytes.
void util_tile_rgba8(uint8_t *dst, const uint8_t *src, int size);
