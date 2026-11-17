#include "art.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

#define ART_CACHE_SLOTS 6
#define ART_MAX_BYTES   (6u * 1024u * 1024u)

typedef struct {
    bool used;
    bool valid;
    char key[420];
    C3D_Tex tex;
    Tex3DS_SubTexture subtex;
    C2D_Image image;
} ArtSlot;

static ArtSlot s_slots[ART_CACHE_SLOTS];
static int s_next_slot;
static uint8_t s_tiled[ART_PIXELS * ART_PIXELS * 4];
static uint8_t s_square[ART_PIXELS * ART_PIXELS * 4];

// Centre-crops to a square and box-filters down to ART_PIXELS.
static void make_square(const uint8_t *src, int w, int h, uint8_t *dst)
{
    int sq = (w < h) ? w : h;
    int ox = (w - sq) / 2;
    int oy = (h - sq) / 2;
    float scale = (float)sq / (float)ART_PIXELS;

    for (int dy = 0; dy < ART_PIXELS; dy++) {
        int sy0 = oy + (int)((float)dy * scale);
        int sy1 = oy + (int)((float)(dy + 1) * scale);
        if (sy1 <= sy0)
            sy1 = sy0 + 1;
        if (sy1 > oy + sq)
            sy1 = oy + sq;

        for (int dx = 0; dx < ART_PIXELS; dx++) {
            int sx0 = ox + (int)((float)dx * scale);
            int sx1 = ox + (int)((float)(dx + 1) * scale);
            if (sx1 <= sx0)
                sx1 = sx0 + 1;
            if (sx1 > ox + sq)
                sx1 = ox + sq;

            uint32_t r = 0, g = 0, b = 0, a = 0;
            uint32_t n = 0;
            for (int sy = sy0; sy < sy1; sy++) {
                const uint8_t *row = src + (((size_t)sy * (size_t)w) + (size_t)sx0) * 4;
                for (int sx = sx0; sx < sx1; sx++) {
                    r += row[0];
                    g += row[1];
                    b += row[2];
                    a += row[3];
                    row += 4;
                    n++;
                }
            }
            if (n == 0)
                n = 1;
            uint8_t *o = dst + (((size_t)dy * ART_PIXELS) + (size_t)dx) * 4;
            o[0] = (uint8_t)(r / n);
            o[1] = (uint8_t)(g / n);
            o[2] = (uint8_t)(b / n);
            o[3] = (uint8_t)(a / n);
        }
    }
}

static bool load_cover(ArtSlot *slot, const char *path, uint32_t offset, uint32_t size)
{
    if (!path || size < 64 || size > ART_MAX_BYTES)
        return false;

    FILE *fp = fopen(path, "rb");
    if (!fp)
        return false;

    if (fseek(fp, (long)offset, SEEK_SET) != 0) {
        fclose(fp);
        return false;
    }

    uint8_t *raw = (uint8_t *)malloc(size);
    if (!raw) {
        fclose(fp);
        return false;
    }

    size_t got = fread(raw, 1, size, fp);
    fclose(fp);
    if (got < 64) {
        free(raw);
        return false;
    }

    int w = 0, h = 0, comp = 0;
    uint8_t *pixels = stbi_load_from_memory(raw, (int)got, &w, &h, &comp, 4);
    free(raw);

    if (!pixels || w <= 0 || h <= 0) {
        if (pixels)
            stbi_image_free(pixels);
        return false;
    }
    if (w > 12000 || h > 12000) {
        stbi_image_free(pixels);
        return false;
    }

    make_square(pixels, w, h, s_square);
    stbi_image_free(pixels);

    util_tile_rgba8(s_tiled, s_square, ART_PIXELS);

    if (!C3D_TexInit(&slot->tex, ART_PIXELS, ART_PIXELS, GPU_RGBA8))
        return false;

    C3D_TexSetFilter(&slot->tex, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&slot->tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    C3D_TexUpload(&slot->tex, s_tiled);

    // citro2d treats top < bottom as "this subtexture is stored rotated a
    // quarter turn" and transposes the UVs, which draws the cover on its side.
    // An upright texture therefore wants top 1, bottom 0.
    slot->subtex.width = ART_PIXELS;
    slot->subtex.height = ART_PIXELS;
    slot->subtex.left = 0.0f;
    slot->subtex.top = 1.0f;
    slot->subtex.right = 1.0f;
    slot->subtex.bottom = 0.0f;

    slot->image.tex = &slot->tex;
    slot->image.subtex = &slot->subtex;
    return true;
}

void art_init(void)
{
    memset(s_slots, 0, sizeof(s_slots));
    s_next_slot = 0;
}

C2D_Image *art_get(const char *key, const char *path, uint32_t offset, uint32_t size)
{
    if (!key || !key[0] || size == 0)
        return NULL;

    for (int i = 0; i < ART_CACHE_SLOTS; i++) {
        if (s_slots[i].used && strcmp(s_slots[i].key, key) == 0)
            return s_slots[i].valid ? &s_slots[i].image : NULL;
    }

    ArtSlot *slot = &s_slots[s_next_slot];
    s_next_slot = (s_next_slot + 1) % ART_CACHE_SLOTS;

    if (slot->used) {
        C3D_TexDelete(&slot->tex);
        memset(slot, 0, sizeof(*slot));
    }

    slot->used = true;
    util_copy(slot->key, sizeof(slot->key), key);

    // Cover art decoding happens during rendering, so the frame stays valid.
    slot->valid = load_cover(slot, path, offset, size);
    return slot->valid ? &slot->image : NULL;
}

void art_shutdown(void)
{
    for (int i = 0; i < ART_CACHE_SLOTS; i++) {
        if (s_slots[i].used) {
            C3D_TexDelete(&s_slots[i].tex);
            memset(&s_slots[i], 0, sizeof(s_slots[i]));
        }
    }
}
