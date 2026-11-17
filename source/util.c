#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void util_copy(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0)
        return;
    if (!src) {
        dst[0] = 0;
        return;
    }
    size_t i = 0;
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

void util_trim(char *s)
{
    if (!s)
        return;
    size_t len = strlen(s);
    while (len > 0 && (unsigned char)s[len - 1] <= ' ')
        s[--len] = 0;
    size_t start = 0;
    while (s[start] && (unsigned char)s[start] <= ' ' && start < len)
        start++;
    if (start > 0)
        memmove(s, s + start, strlen(s + start) + 1);
}

int util_cmp_icase(const char *a, const char *b)
{
    if (!a)
        a = "";
    if (!b)
        b = "";
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb)
            return ca - cb;
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

bool util_ends_with_icase(const char *s, const char *suffix)
{
    if (!s || !suffix)
        return false;
    size_t sl = strlen(s);
    size_t fl = strlen(suffix);
    if (fl > sl)
        return false;
    return util_cmp_icase(s + sl - fl, suffix) == 0;
}

bool util_starts_with_icase(const char *s, const char *prefix)
{
    if (!s || !prefix)
        return false;
    while (*prefix) {
        if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix))
            return false;
        s++;
        prefix++;
    }
    return true;
}

void util_dirname(const char *path, char *out, size_t cap)
{
    if (!path || !out || cap == 0)
        return;
    const char *slash = strrchr(path, '/');
    if (!slash) {
        out[0] = 0;
        return;
    }
    size_t n = (size_t)(slash - path);
    if (n >= cap)
        n = cap - 1;
    memcpy(out, path, n);
    out[n] = 0;
}

void util_basename(const char *path, char *out, size_t cap)
{
    if (!path || !out || cap == 0)
        return;
    const char *slash = strrchr(path, '/');
    util_copy(out, cap, slash ? slash + 1 : path);
}

void util_strip_ext(char *name)
{
    if (!name)
        return;
    char *slash = strrchr(name, '/');
    char *dot = strrchr(name, '.');
    if (dot && (!slash || dot > slash))
        *dot = 0;
}

// True for "/x", "\\x" and device paths such as "sdmc:/x" or "C:\\x".
static bool path_is_absolute(const char *p)
{
    if (!p || !p[0])
        return false;
    if (p[0] == '/' || p[0] == '\\')
        return true;
    for (int i = 0; p[i] && p[i] != '/' && p[i] != '\\'; i++) {
        if (p[i] == ':' && i > 0)
            return true;
    }
    return false;
}

// Length of a leading device prefix including its slash, e.g. 6 for "sdmc:/".
static size_t device_prefix_len(const char *p)
{
    size_t i = 0;
    while (p[i] && p[i] != '/') {
        if (p[i] == ':' && i > 0) {
            size_t k = i + 1;
            if (p[k] == '/')
                k++;
            return k;
        }
        i++;
    }
    return 0;
}

void util_join_normalized(char *out, size_t cap, const char *dir, const char *entry)
{
    if (!out || cap == 0)
        return;
    out[0] = 0;
    if (!entry)
        return;

    char tmp[1024];
    size_t elen = strlen(entry);

    bool absolute = path_is_absolute(entry);
    (void)elen;
    if (absolute || !dir || !dir[0])
        util_copy(tmp, sizeof(tmp), entry);
    else
        snprintf(tmp, sizeof(tmp), "%s/%s", dir, entry);

    for (char *p = tmp; *p; p++) {
        if (*p == '\\')
            *p = '/';
    }

    char res[1024];
    size_t rlen = 0;
    const char *p = tmp;

    size_t dev = device_prefix_len(tmp);
    if (dev > 0 && dev < sizeof(res) - 2) {
        memcpy(res, tmp, dev);
        rlen = dev;
        if (res[rlen - 1] != '/')
            res[rlen++] = '/';
        p = tmp + dev;
    } else if (tmp[0] == '/') {
        res[rlen++] = '/';
        p = tmp + 1;
    }

    while (*p) {
        const char *slash = strchr(p, '/');
        size_t len = slash ? (size_t)(slash - p) : strlen(p);

        if (len == 0 || (len == 1 && p[0] == '.')) {
            // skip
        } else if (len == 2 && p[0] == '.' && p[1] == '.') {
            if (rlen > 0) {
                if (res[rlen - 1] == '/')
                    rlen--;
                while (rlen > 0 && res[rlen - 1] != '/')
                    rlen--;
            }
        } else {
            if (rlen > 0 && res[rlen - 1] != '/' && rlen + 1 < sizeof(res))
                res[rlen++] = '/';
            if (rlen + len < sizeof(res)) {
                memcpy(res + rlen, p, len);
                rlen += len;
            }
        }

        if (!slash)
            break;
        p = slash + 1;
    }

    while (rlen > 1 && res[rlen - 1] == '/' && res[rlen - 2] != ':')
        rlen--;

    res[rlen] = 0;
    util_copy(out, cap, res);
}

void util_tile_rgba8(uint8_t *dst, const uint8_t *src, int size)
{
    const int tile = 8;
    int tiles = size / tile;

    for (int ty = 0; ty < tiles; ty++) {
        for (int tx = 0; tx < tiles; tx++) {
            // Tiles run row-major across the image and the texels inside one
            // tile are in Z order. Each texel is stored A,B,G,R because that
            // is how the GPU reads RGBA8, not the R,G,B,A order of `src`.
            uint8_t *out = dst + (size_t)(ty * tiles + tx) * (size_t)(tile * tile * 4);
            for (int y = 0; y < tile; y++) {
                const uint8_t *row = src + (((size_t)(ty * tile + y) * (size_t)size) +
                                            (size_t)(tx * tile)) * 4;
                for (int x = 0; x < tile; x++) {
                    uint32_t z = 0;
                    for (int i = 0; i < 3; i++) {
                        z |= (uint32_t)((x >> i) & 1) << (2 * i);
                        z |= (uint32_t)((y >> i) & 1) << (2 * i + 1);
                    }

                    const uint8_t *in = row + (size_t)x * 4;
                    uint8_t *texel = out + (size_t)z * 4;
                    texel[0] = in[3];
                    texel[1] = in[2];
                    texel[2] = in[1];
                    texel[3] = in[0];
                }
            }
        }
    }
}

void util_format_time(uint32_t ms, char *out, size_t cap)
{
    if (!out || cap == 0)
        return;
    if (ms == 0) {
        snprintf(out, cap, "--:--");
        return;
    }
    uint32_t total = ms / 1000u;
    unsigned h = (unsigned)(total / 3600u);
    unsigned m = (unsigned)((total / 60u) % 60u);
    unsigned s = (unsigned)(total % 60u);
    if (h > 0)
        snprintf(out, cap, "%u:%02u:%02u", h, m, s);
    else
        snprintf(out, cap, "%u:%02u", m, s);
}
