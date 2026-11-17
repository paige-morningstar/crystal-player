#pragma once

#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>
#include <stdint.h>

#define ART_PIXELS 256

// Returns a cached texture for the cover embedded in `path` at [offset, offset+size),
// or NULL when there is no usable picture. `key` identifies the picture; repeated
// calls with the same key reuse the loaded texture.
C2D_Image *art_get(const char *key, const char *path, uint32_t offset, uint32_t size);

void art_init(void);
void art_shutdown(void);
