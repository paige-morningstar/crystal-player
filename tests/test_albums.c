/* Host-side tests for album grouping. Run with: make test
 *
 * These compile the player's plain-C helpers for the build machine, so they
 * need no 3DS toolchain and no hardware.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "library.h"

static int s_fails;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
            s_fails++;                                                     \
        }                                                                  \
    } while (0)

// Adds a track the way the scanner does: album artist may be empty.
static void add(const char *album, const char *album_artist, const char *artist,
                const char *title, int track_no)
{
    Track *t = &g_lib.tracks[g_lib.track_count++];
    memset(t, 0, sizeof(*t));
    snprintf(t->album, sizeof(t->album), "%s", album);
    snprintf(t->album_artist, sizeof(t->album_artist), "%s", album_artist);
    snprintf(t->artist, sizeof(t->artist), "%s", artist);
    snprintf(t->title, sizeof(t->title), "%s", title);
    snprintf(t->path, sizeof(t->path), "/music/%s.mp3", title);
    t->track_no = (uint16_t)track_no;
}

int main(void)
{
    g_lib.tracks = calloc(32, sizeof(Track));
    if (!g_lib.tracks) {
        printf("FAIL: out of memory\n");
        return 1;
    }

    // One album, tagged in the wrong order.
    add("Hollow Knight", "Christopher Larkin", "Christopher Larkin", "Finale", 4);            // 0
    add("Hollow Knight", "Christopher Larkin", "Christopher Larkin", "Enter Hallownest", 1); // 1
    add("Hollow Knight", "Christopher Larkin", "Christopher Larkin", "Dirtmouth", 3);        // 2
    add("Hollow Knight", "Christopher Larkin", "Christopher Larkin", "Crossroads", 2);       // 3
    add("Hollow Knight", "Christopher Larkin", "Christopher Larkin", "Hornet", 5);           // 4

    // A compilation: one album artist, a different track artist per song.
    add("Hits of the 90s", "Various Artists", "Band One", "Song A", 1);   // 5
    add("Hits of the 90s", "Various Artists", "Band Two", "Song B", 2);   // 6
    add("Hits of the 90s", "Various Artists", "Band Three", "Song C", 3); // 7

    // No album artist tag: the track artist identifies the album.
    add("Zelda", "", "Koji Kondo", "Title Theme", 1);          // 8
    add("Drift", "", "Someone Else", "Drift", 1);              // 9
    add("Hollow Knight", "", "Some Guest", "Cameo", 6);        // 10

    library_build_albums();

    // Artist order: Christopher Larkin, Koji Kondo, Some Guest, Someone Else,
    // Various Artists.
    CHECK(g_lib.album_count == 5);

    CHECK(strcmp(g_lib.albums[0].name, "Hollow Knight") == 0);
    CHECK(strcmp(g_lib.albums[0].artist, "Christopher Larkin") == 0);
    CHECK(g_lib.albums[0].count == 5);
    const int want[] = { 1, 3, 2, 0, 4 }; // track numbers 1..5
    for (int i = 0; i < 5; i++)
        CHECK(g_lib.albums[0].tracks[i] == want[i]);

    CHECK(strcmp(g_lib.albums[1].name, "Zelda") == 0);

    // Same title, different album artist: a separate album.
    CHECK(strcmp(g_lib.albums[2].name, "Hollow Knight") == 0);
    CHECK(strcmp(g_lib.albums[2].artist, "Some Guest") == 0);
    CHECK(g_lib.albums[2].count == 1);
    CHECK(g_lib.albums[2].tracks[0] == 10);

    CHECK(strcmp(g_lib.albums[3].name, "Drift") == 0);

    // The compilation did not split into one album per track artist.
    CHECK(strcmp(g_lib.albums[4].name, "Hits of the 90s") == 0);
    CHECK(strcmp(g_lib.albums[4].artist, "Various Artists") == 0);
    CHECK(g_lib.albums[4].count == 3);
    for (int i = 0; i < 3; i++)
        CHECK(g_lib.albums[4].tracks[i] == 5 + i);
    CHECK(strcmp(g_lib.tracks[g_lib.albums[4].tracks[1]].artist, "Band Two") == 0);

    CHECK(library_album_of_track(5) == 4);
    CHECK(library_album_of_track(6) == 4);
    CHECK(library_album_of_track(7) == 4);
    CHECK(library_album_of_track(0) == 0);
    CHECK(library_album_of_track(-1) == -1);
    CHECK(library_album_of_track(99) == -1);

    library_free();
    CHECK(g_lib.album_count == 0);

    if (s_fails) {
        printf("album tests: %d FAILED\n", s_fails);
        return 1;
    }
    printf("album tests: all passed\n");
    return 0;
}
