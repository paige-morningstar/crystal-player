#include "id3.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static uint32_t rd_be32(const uint8_t *b)
{
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}

static uint16_t rd_be16(const uint8_t *b)
{
    return (uint16_t)(((uint16_t)b[0] << 8) | (uint16_t)b[1]);
}

static uint32_t rd_syncsafe32(const uint8_t *b)
{
    return ((uint32_t)(b[0] & 0x7F) << 21) | ((uint32_t)(b[1] & 0x7F) << 14) |
           ((uint32_t)(b[2] & 0x7F) << 7) | (uint32_t)(b[3] & 0x7F);
}

static size_t utf8_put(char *dst, size_t cap, size_t pos, uint32_t cp)
{
    if (cp < 0x80) {
        if (pos + 1 < cap)
            dst[pos++] = (char)cp;
    } else if (cp < 0x800) {
        if (pos + 2 < cap) {
            dst[pos++] = (char)(0xC0 | (cp >> 6));
            dst[pos++] = (char)(0x80 | (cp & 0x3F));
        }
    } else if (cp < 0x10000) {
        if (pos + 3 < cap) {
            dst[pos++] = (char)(0xE0 | (cp >> 12));
            dst[pos++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            dst[pos++] = (char)(0x80 | (cp & 0x3F));
        }
    } else {
        if (pos + 4 < cap) {
            dst[pos++] = (char)(0xF0 | (cp >> 18));
            dst[pos++] = (char)(0x80 | ((cp >> 12) & 0x3F));
            dst[pos++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            dst[pos++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    return pos;
}

static void sanitize(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && (unsigned char)s[len - 1] <= ' ')
        s[--len] = 0;
    size_t start = 0;
    while (s[start] && (unsigned char)s[start] <= ' ' && start + 1 < len)
        start++;
    if (start > 0)
        memmove(s, s + start, strlen(s + start) + 1);
    for (char *p = s; *p; p++) {
        if ((unsigned char)*p < 0x20)
            *p = ' ';
    }
}

// Decodes a text frame payload (encoding byte + bytes) into UTF-8.
static void decode_text(const uint8_t *data, size_t len, char *out, size_t cap)
{
    if (!out || cap == 0)
        return;
    out[0] = 0;
    if (!data || len == 0)
        return;

    uint8_t enc = data[0];
    const uint8_t *p = data + 1;
    size_t n = len - 1;
    size_t pos = 0;

    if (enc == 0) {
        for (size_t i = 0; i < n; i++) {
            if (p[i] == 0)
                break;
            pos = utf8_put(out, cap, pos, p[i]);
        }
    } else if (enc == 1 || enc == 2) {
        bool be = (enc == 2);
        size_t i = 0;
        if (n >= 2 && p[0] == 0xFF && p[1] == 0xFE) {
            be = false;
            i = 2;
        } else if (n >= 2 && p[0] == 0xFE && p[1] == 0xFF) {
            be = true;
            i = 2;
        }
        uint32_t high = 0;
        while (i + 1 < n) {
            uint32_t u = be ? (((uint32_t)p[i] << 8) | p[i + 1])
                            : ((uint32_t)p[i] | ((uint32_t)p[i + 1] << 8));
            i += 2;
            if (u == 0)
                break;
            if (u >= 0xD800 && u <= 0xDBFF) {
                high = u;
                continue;
            }
            if (u >= 0xDC00 && u <= 0xDFFF && high) {
                u = 0x10000 + ((high - 0xD800) << 10) + (u - 0xDC00);
                high = 0;
            }
            pos = utf8_put(out, cap, pos, u);
        }
    } else {
        size_t i = 0;
        if (n >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF)
            i = 3;
        for (; i < n && p[i] && pos + 1 < cap; i++) {
            if (p[i] < 0x20)
                continue;
            out[pos++] = (char)p[i];
        }
    }

    if (pos < cap)
        out[pos] = 0;
    else
        out[cap - 1] = 0;
    sanitize(out);
}

static void latin1_to_utf8(const uint8_t *src, size_t n, char *out, size_t cap)
{
    if (!out || cap == 0)
        return;
    size_t pos = 0;
    for (size_t i = 0; i < n && src[i]; i++)
        pos = utf8_put(out, cap, pos, src[i]);
    if (pos < cap)
        out[pos] = 0;
    else
        out[cap - 1] = 0;
    sanitize(out);
}

static void read_text_at(FILE *fp, long pos, uint32_t size, char *out, size_t cap)
{
    if (!out || cap == 0)
        return;
    out[0] = 0;
    if (size == 0)
        return;
    uint8_t buf[1024];
    size_t n = size < sizeof(buf) ? size : sizeof(buf);
    if (fseek(fp, pos, SEEK_SET) != 0)
        return;
    n = fread(buf, 1, n, fp);
    if (n > 0)
        decode_text(buf, n, out, cap);
}

static void read_apic(FILE *fp, long pos, uint32_t size, int ver, Id3Tags *t)
{
    if (t->art_type != ART_NONE || t->art_size != 0)
        return; // first picture wins
    if (size < 24 || size > 32u * 1024u * 1024u)
        return;

    uint8_t buf[1024];
    size_t n = size < sizeof(buf) ? size : sizeof(buf);
    if (fseek(fp, pos, SEEK_SET) != 0)
        return;
    n = fread(buf, 1, n, fp);
    if (n < 8)
        return;

    uint8_t enc = buf[0];
    int type = ART_NONE;
    size_t i;

    if (ver == 2) {
        if (buf[1] == 'J' && buf[2] == 'P' && buf[3] == 'G')
            type = ART_JPEG;
        else if (buf[1] == 'P' && buf[2] == 'N' && buf[3] == 'G')
            type = ART_PNG;
        i = 5; // encoding + 3-char format + picture type
    } else {
        size_t m = 1;
        while (m < n && buf[m])
            m++;
        if (m + 1 >= n)
            return;
        if (!strncasecmp((const char *)buf + 1, "image/jpeg", 10) ||
            !strncasecmp((const char *)buf + 1, "image/jpg", 9))
            type = ART_JPEG;
        else if (!strncasecmp((const char *)buf + 1, "image/png", 9))
            type = ART_PNG;
        i = m + 2; // skip NUL + picture type
    }

    if (enc == 1 || enc == 2) {
        while (i + 1 < n && !(buf[i] == 0 && buf[i + 1] == 0))
            i += 2;
        i += 2;
    } else {
        while (i < n && buf[i])
            i++;
        i += 1;
    }

    if (i >= n || i >= size)
        return; // description ran past what we could inspect

    if (type == ART_NONE) {
        uint8_t sig[4];
        if (fseek(fp, pos + (long)i, SEEK_SET) == 0 && fread(sig, 1, 4, fp) == 4) {
            if (sig[0] == 0xFF && sig[1] == 0xD8)
                type = ART_JPEG;
            else if (sig[0] == 0x89 && sig[1] == 'P' && sig[2] == 'N' && sig[3] == 'G')
                type = ART_PNG;
        }
    }

    if (type == ART_NONE || size - (uint32_t)i < 64)
        return;

    t->art_type = (uint8_t)type;
    t->art_offset = (uint32_t)(pos + (long)i);
    t->art_size = size - (uint32_t)i;
}

static void parse_v2(FILE *fp, Id3Tags *t)
{
    uint8_t hdr[10];
    if (fseek(fp, 0, SEEK_SET) != 0)
        return;
    if (fread(hdr, 1, 10, fp) != 10)
        return;
    if (memcmp(hdr, "ID3", 3) != 0)
        return;

    int ver = hdr[3];
    uint8_t flags = hdr[5];
    uint32_t tagSize = rd_syncsafe32(hdr + 6);
    if (ver < 2 || ver > 4 || tagSize < 10 || tagSize > 64u * 1024u * 1024u)
        return;

    long pos = 10;
    long tagEnd = 10 + (long)tagSize;

    if (flags & 0x40) { // extended header
        uint8_t ext[4];
        if (fseek(fp, pos, SEEK_SET) != 0 || fread(ext, 1, 4, fp) != 4)
            return;
        uint32_t extSize = (ver == 4) ? rd_syncsafe32(ext) : rd_be32(ext);
        pos += (ver == 3) ? 4 + (long)extSize : (long)extSize;
        if (pos >= tagEnd || extSize > tagSize)
            return;
    }

    const int hdrLen = (ver == 2) ? 6 : 10;
    const int idLen = (ver == 2) ? 3 : 4;
    int guard = 0;

    while (pos + hdrLen <= tagEnd && guard++ < 8192) {
        uint8_t fh[10];
        if (fseek(fp, pos, SEEK_SET) != 0)
            break;
        if (fread(fh, 1, (size_t)hdrLen, fp) != (size_t)hdrLen)
            break;
        if (fh[0] == 0)
            break; // padding

        bool ok = true;
        for (int i = 0; i < idLen; i++) {
            uint8_t c = fh[i];
            if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) {
                ok = false;
                break;
            }
        }
        if (!ok)
            break;

        uint32_t fsize;
        if (ver == 2)
            fsize = ((uint32_t)fh[3] << 16) | ((uint32_t)fh[4] << 8) | (uint32_t)fh[5];
        else if (ver == 4)
            fsize = rd_syncsafe32(fh + 4);
        else
            fsize = rd_be32(fh + 4);

        long dataPos = pos + hdrLen;
        if (fsize == 0 || dataPos + (long)fsize > tagEnd) {
            pos = dataPos + (long)fsize;
            continue;
        }

        char id[5];
        memcpy(id, fh, (size_t)idLen);
        id[idLen] = 0;

        uint16_t fflags = (ver == 2) ? 0 : rd_be16(fh + 8);
        bool skip = false;
        if (ver == 3 && (fflags & 0x00C0))
            skip = true; // compressed/encrypted
        if (ver == 4 && (fflags & 0x000C))
            skip = true;

        if (!skip) {
            if (!strcmp(id, "TIT2") || !strcmp(id, "TT2")) {
                read_text_at(fp, dataPos, fsize, t->title, sizeof(t->title));
            } else if (!strcmp(id, "TPE1") || !strcmp(id, "TP1")) {
                read_text_at(fp, dataPos, fsize, t->artist, sizeof(t->artist));
            } else if (!strcmp(id, "TALB") || !strcmp(id, "TAL")) {
                read_text_at(fp, dataPos, fsize, t->album, sizeof(t->album));
            } else if (!strcmp(id, "TPE2") || !strcmp(id, "TP2")) {
                read_text_at(fp, dataPos, fsize, t->album_artist, sizeof(t->album_artist));
            } else if (!strcmp(id, "TRCK") || !strcmp(id, "TRK")) {
                char s[40] = {0};
                read_text_at(fp, dataPos, fsize, s, sizeof(s));
                t->track_no = (uint16_t)atoi(s);
            } else if (!strcmp(id, "TPOS") || !strcmp(id, "TPA")) {
                char s[40] = {0};
                read_text_at(fp, dataPos, fsize, s, sizeof(s));
                t->disc_no = (uint16_t)atoi(s);
            } else if (!strcmp(id, "TYER") || !strcmp(id, "TYE") || !strcmp(id, "TDRC")) {
                char s[16] = {0};
                if (t->year == 0) {
                    read_text_at(fp, dataPos, fsize, s, sizeof(s));
                    t->year = (uint16_t)atoi(s);
                }
            } else if (!strcmp(id, "TLEN") || !strcmp(id, "TLE")) {
                char s[24] = {0};
                read_text_at(fp, dataPos, fsize, s, sizeof(s));
                if (t->tlen_ms == 0)
                    t->tlen_ms = (uint32_t)strtoul(s, NULL, 10);
            } else if (!strcmp(id, "APIC") || !strcmp(id, "PIC")) {
                read_apic(fp, dataPos, fsize, ver, t);
            }
        }

        pos = dataPos + (long)fsize;
    }
}

static void parse_v1(FILE *fp, Id3Tags *t)
{
    uint8_t buf[128];
    if (fseek(fp, -128, SEEK_END) != 0)
        return;
    if (fread(buf, 1, 128, fp) != 128)
        return;
    if (memcmp(buf, "TAG", 3) != 0)
        return;

    if (!t->title[0])
        latin1_to_utf8(buf + 3, 30, t->title, sizeof(t->title));
    if (!t->artist[0])
        latin1_to_utf8(buf + 33, 30, t->artist, sizeof(t->artist));
    if (!t->album[0])
        latin1_to_utf8(buf + 63, 30, t->album, sizeof(t->album));
    if (t->year == 0) {
        char year[8];
        latin1_to_utf8(buf + 93, 4, year, sizeof(year));
        t->year = (uint16_t)atoi(year);
    }
    // ID3v1.1 stores the track number where the comment used to end.
    if (t->track_no == 0 && buf[97 + 28] == 0 && buf[97 + 29] != 0)
        t->track_no = buf[97 + 29];
}

void id3_read(const char *path, Id3Tags *out)
{
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    if (!path)
        return;

    FILE *fp = fopen(path, "rb");
    if (!fp)
        return;

    parse_v2(fp, out);
    parse_v1(fp, out);

    fclose(fp);
}
