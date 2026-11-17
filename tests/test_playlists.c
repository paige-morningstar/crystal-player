/* Host-side tests for finding a track from a playlist entry. Run with: make test
 *
 * Playlists are written by other programs, so entries are often absolute paths
 * from another machine or relative to the wrong folder. The library has to find
 * the song anyway.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "library.h"
#include "m3u8.h"

static int s_fails;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
            s_fails++;                                                     \
        }                                                                  \
    } while (0)

static int s_resolved;
static int s_unresolved;

static void resolve(void *user, const char *path, const char *title)
{
    (void)user;
    (void)title;
    if (library_find_path(path) >= 0)
        s_resolved++;
    else
        s_unresolved++;
}

static void add(const char *folder, const char *file)
{
    Track *t = &g_lib.tracks[g_lib.track_count++];
    memset(t, 0, sizeof(*t));
    snprintf(t->folder, sizeof(t->folder), "%s", folder);
    snprintf(t->path, sizeof(t->path), "sdmc:/music/%s/%s", folder, file);
}

int main(void)
{
    g_lib.tracks = calloc(16, sizeof(Track));
    if (!g_lib.tracks) {
        printf("FAIL: out of memory\n");
        return 1;
    }

    add("Hollow Knight", "02 Dirtmouth.mp3");   // 0
    add("Hollow Knight", "03 Crossroads.mp3"); // 1
    add("Hits", "01 Song.mp3");                // 2
    add("Other", "01 Song.mp3");               // 3
    add("Zelda", "01 Title.mp3");              // 4

    // Exact paths, including the sloppy normalisation playlists often contain.
    CHECK(library_find_path("sdmc:/music/Hollow Knight/02 Dirtmouth.mp3") == 0);
    CHECK(library_find_path("sdmc:/music//Hollow Knight/./02 Dirtmouth.mp3") == 0);
    CHECK(library_find_path("sdmc:/music/Hits/01 Song.mp3") == 2);

    // Resuming playback must never guess: only the exact file counts, even when
    // another album has a song with the same name.
    CHECK(library_find_exact("sdmc:/music/Hits/01 Song.mp3") == 2);
    CHECK(library_find_exact("sdmc:/music//Hits/./01 Song.mp3") == 2);
    CHECK(library_find_exact("sdmc:/music/Other/01 Song.mp3") == 3);
    CHECK(library_find_exact("01 Song.mp3") == -1);
    CHECK(library_find_exact("../Hits/01 Song.mp3") == -1);
    CHECK(library_find_exact("/Users/someone/Music/02 Dirtmouth.mp3") == -1);
    CHECK(library_find_exact("sdmc:/music/Gone/01 Song.mp3") == -1);

    // Paths from another machine (what Apple Music exports) and relative ones.
    CHECK(library_find_path("/Users/someone/Music/Media/Music/Hollow Knight/02 Dirtmouth.mp3") == 0);
    CHECK(library_find_path("C:/Users/someone/Music/02 Dirtmouth.mp3") == 0);
    CHECK(library_find_path("Hollow Knight/03 Crossroads.mp3") == 1);
    CHECK(library_find_path("../Zelda/01 Title.mp3") == 4);

    // Case does not matter.
    CHECK(library_find_path("02 DIRTMOUTH.mp3") == 0);

    // Duplicate file names: the folder picks one, a bare name is ambiguous.
    CHECK(library_find_path("../Hits/01 Song.mp3") == 2);
    CHECK(library_find_path("../Other/01 Song.mp3") == 3);
    CHECK(library_find_path("01 Song.mp3") == -1);

    // Nothing to match.
    CHECK(library_find_path("") == -1);
    CHECK(library_find_path("does not exist.mp3") == -1);
    CHECK(library_find_path(NULL) == -1);

    // End to end: a real .m3u8 with foreign paths, parsed then resolved the way
    // the library does it.
    const char *playlist = "/tmp/crystal_player_test.m3u8";
    FILE *fp = fopen(playlist, "wb");
    CHECK(fp != NULL);
    if (fp) {
        fprintf(fp, "#EXTM3U\r\n");
        fprintf(fp, "#EXTINF:120,Dirtmouth\r\n");
        fprintf(fp, "C:\\Users\\someone\\Music\\02 Dirtmouth.mp3\r\n");
        fprintf(fp, "/Users/someone/Music/Media/Music/Hollow Knight/03 Crossroads.mp3\r\n");
        fprintf(fp, "\r\n");
        fprintf(fp, "../Zelda/01 Title.mp3\r\n");
        fclose(fp);

        CHECK(m3u8_parse(playlist, resolve, NULL) == 3);
        CHECK(s_resolved == 3);
        CHECK(s_unresolved == 0);
        remove(playlist);
    }

    // The same playlist as Music exports it: \r after every line, nothing else.
    fp = fopen(playlist, "wb");
    CHECK(fp != NULL);
    if (fp) {
        fprintf(fp, "#EXTM3U\r#EXTINF:120,Dirtmouth\r"
                    "/Users/someone/Music/Media/Music/Hollow Knight/02 Dirtmouth.mp3\r"
                    "Hollow Knight/03 Crossroads.mp3\r"
                    "../Zelda/01 Title.mp3\r");
        fclose(fp);

        s_resolved = s_unresolved = 0;
        CHECK(m3u8_parse(playlist, resolve, NULL) == 3);
        CHECK(s_resolved == 3);
        CHECK(s_unresolved == 0);
        remove(playlist);
    }

    library_free();
    if (s_fails) {
        printf("playlist tests: %d FAILED\n", s_fails);
        return 1;
    }
    printf("playlist tests: all passed\n");
    return 0;
}
