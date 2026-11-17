#include "m3u8.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "util.h"

bool m3u8_is_playlist(const char *filename)
{
    return util_ends_with_icase(filename, ".m3u8") || util_ends_with_icase(filename, ".m3u");
}

// Reads one line, treating \n, \r and \r\n all as the end of a line. Music
// exports playlists with plain \r, which fgets() would read as one long line.
static bool read_line(FILE *fp, char *buf, size_t size)
{
    size_t len = 0;
    int c;

    while ((c = fgetc(fp)) != EOF && c != '\n' && c != '\r') {
        if (len + 1 < size)
            buf[len++] = (char)c;
    }
    buf[len] = 0;

    if (c == '\r') {
        c = fgetc(fp);
        if (c != '\n' && c != EOF)
            ungetc(c, fp);
    }

    return len > 0 || c != EOF;
}

int m3u8_parse(const char *playlist_path, m3u8_entry_fn fn, void *user)
{
    if (!playlist_path || !fn)
        return 0;

    FILE *fp = fopen(playlist_path, "rb");
    if (!fp)
        return 0;

    char dir[512];
    util_dirname(playlist_path, dir, sizeof(dir));

    char line[1024];
    char title[256];
    int count = 0;
    bool first = true;

    title[0] = 0;

    while (read_line(fp, line, sizeof(line))) {
        if (first) {
            first = false;
            // Strip a UTF-8 BOM if the file was written by a Windows editor.
            if ((unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB &&
                (unsigned char)line[2] == 0xBF)
                memmove(line, line + 3, strlen(line + 3) + 1);
        }

        util_trim(line);
        if (line[0] == 0)
            continue;

        if (line[0] == '#') {
            if (util_starts_with_icase(line, "#EXTINF:")) {
                char *comma = strchr(line, ',');
                if (comma && comma[1]) {
                    util_copy(title, sizeof(title), comma + 1);
                    util_trim(title);
                } else {
                    title[0] = 0;
                }
            }
            continue;
        }

        char resolved[600];
        util_join_normalized(resolved, sizeof(resolved), dir, line);
        fn(user, resolved, title[0] ? title : NULL);
        title[0] = 0;
        count++;
    }

    fclose(fp);
    return count;
}
