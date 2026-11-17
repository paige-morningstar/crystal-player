#include "library.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "id3.h"
#include "m3u8.h"
#include "mp3info.h"
#include "util.h"

Library g_lib;

typedef struct {
    char **items;
    int count;
    int cap;
} StrList;

static bool strlist_push(StrList *l, const char *s)
{
    if (l->count == l->cap) {
        int cap = l->cap ? l->cap * 2 : 16;
        char **p = (char **)realloc(l->items, sizeof(char *) * (size_t)cap);
        if (!p)
            return false;
        l->items = p;
        l->cap = cap;
    }
    size_t len = strlen(s) + 1;
    char *copy = (char *)malloc(len);
    if (!copy)
        return false;
    memcpy(copy, s, len);
    l->items[l->count++] = copy;
    return true;
}

static void strlist_free(StrList *l)
{
    for (int i = 0; i < l->count; i++)
        free(l->items[i]);
    free(l->items);
    l->items = NULL;
    l->count = l->cap = 0;
}

static Track *track_alloc(void)
{
    if (g_lib.track_count == g_lib.track_capacity) {
        int cap = g_lib.track_capacity ? g_lib.track_capacity * 2 : 256;
        Track *p = (Track *)realloc(g_lib.tracks, sizeof(Track) * (size_t)cap);
        if (!p)
            return NULL;
        g_lib.tracks = p;
        g_lib.track_capacity = cap;
    }
    Track *t = &g_lib.tracks[g_lib.track_count++];
    memset(t, 0, sizeof(*t));
    return t;
}

static void add_track(const char *abs_path, const char *rel_folder, const char *filename)
{
    Track *t = track_alloc();
    if (!t)
        return;

    util_copy(t->path, sizeof(t->path), abs_path);
    util_copy(t->folder, sizeof(t->folder), rel_folder);

    char base[192];
    util_copy(base, sizeof(base), filename);
    util_strip_ext(base);

    Id3Tags tags;
    id3_read(abs_path, &tags);

    Mp3Info info;
    mp3_probe(abs_path, &info);

    if (tags.title[0])
        util_copy(t->title, sizeof(t->title), tags.title);
    else
        util_copy(t->title, sizeof(t->title), base);

    if (tags.album_artist[0])
        util_copy(t->album_artist, sizeof(t->album_artist), tags.album_artist);

    if (tags.artist[0])
        util_copy(t->artist, sizeof(t->artist), tags.artist);
    else if (tags.album_artist[0])
        util_copy(t->artist, sizeof(t->artist), tags.album_artist);
    else
        util_copy(t->artist, sizeof(t->artist), "Unknown Artist");

    if (tags.album[0]) {
        util_copy(t->album, sizeof(t->album), tags.album);
    } else if (rel_folder[0]) {
        char folder_name[192];
        util_basename(rel_folder, folder_name, sizeof(folder_name));
        util_copy(t->album, sizeof(t->album), folder_name);
    } else {
        util_copy(t->album, sizeof(t->album), "Unknown Album");
    }

    t->track_no = tags.track_no;
    t->disc_no = tags.disc_no;
    t->year = tags.year;
    t->duration_ms = info.duration_ms ? info.duration_ms : tags.tlen_ms;
    t->bitrate = info.bitrate;
    t->sample_rate = info.sample_rate;
    t->channels = info.channels;
    t->art_offset = tags.art_offset;
    t->art_size = tags.art_size;

    t->audio_start = info.audio_start;
}

static void scan_dir(const char *abs, const char *rel, int depth, StrList *playlists,
                     library_progress_fn progress, int *files)
{
    if (depth > 12)
        return;

    DIR *dir = opendir(abs);
    if (!dir)
        return;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        const char *name = ent->d_name;
        if (!name[0] || name[0] == '.')
            continue;

        char child_abs[720];
        snprintf(child_abs, sizeof(child_abs), "%s/%s", abs, name);

        char child_rel[400];
        if (rel[0])
            snprintf(child_rel, sizeof(child_rel), "%s/%s", rel, name);
        else
            util_copy(child_rel, sizeof(child_rel), name);

        struct stat st;
        if (stat(child_abs, &st) != 0)
            continue;

        if (S_ISDIR(st.st_mode)) {
            scan_dir(child_abs, child_rel, depth + 1, playlists, progress, files);
            continue;
        }

        if (util_ends_with_icase(name, ".mp3")) {
            add_track(child_abs, rel, name);
            (*files)++;
            if (progress && (*files % 4) == 0)
                progress(*files, name);
        } else if (m3u8_is_playlist(name)) {
            strlist_push(playlists, child_abs);
        }
    }

    closedir(dir);
}

typedef struct {
    Playlist *pl;
    int capacity;
} PlaylistCollect;

static void playlist_collect_entry(void *user, const char *path, const char *title)
{
    (void)title;
    PlaylistCollect *c = (PlaylistCollect *)user;
    int idx = library_find_path(path);
    if (idx < 0) {
        c->pl->missing++;
        return;
    }
    if (c->pl->count >= c->capacity)
        return;
    c->pl->tracks[c->pl->count++] = idx;
}

static void playlist_count_entry(void *user, const char *path, const char *title)
{
    (void)path;
    (void)title;
    (*(int *)user)++;
}

static void add_playlist(const char *path)
{
    if (g_lib.playlist_count == g_lib.playlist_capacity) {
        int cap = g_lib.playlist_capacity ? g_lib.playlist_capacity * 2 : 8;
        Playlist *p = (Playlist *)realloc(g_lib.playlists, sizeof(Playlist) * (size_t)cap);
        if (!p)
            return;
        g_lib.playlists = p;
        g_lib.playlist_capacity = cap;
    }

    int entries = 0;
    m3u8_parse(path, playlist_count_entry, &entries);
    if (entries <= 0)
        return;

    Playlist *pl = &g_lib.playlists[g_lib.playlist_count];
    memset(pl, 0, sizeof(*pl));
    util_copy(pl->path, sizeof(pl->path), path);

    char name[192];
    util_basename(path, name, sizeof(name));
    util_strip_ext(name);
    util_copy(pl->name, sizeof(pl->name), name);

    int cap = entries < LIB_MAX_PLAYLIST_TRACKS ? entries : LIB_MAX_PLAYLIST_TRACKS;
    pl->tracks = (int *)malloc(sizeof(int) * (size_t)cap);
    if (!pl->tracks)
        return;

    PlaylistCollect collect = {pl, cap};
    m3u8_parse(path, playlist_collect_entry, &collect);
    g_lib.playlist_count++;
}

int library_scan(library_progress_fn progress)
{
    library_free();

    StrList playlists = {0};
    int files = 0;

    if (progress)
        progress(0, "Looking for music...");

    scan_dir(MUSIC_ROOT, "", 0, &playlists, progress, &files);

    for (int i = 0; i < playlists.count; i++)
        add_playlist(playlists.items[i]);
    strlist_free(&playlists);

    library_build_albums();

    if (progress)
        progress(files, "Done");

    return g_lib.track_count;
}

// The artist an album belongs to: the album artist tag when the file has one,
// so a compilation stays one album instead of splitting per track artist.
static const char *album_group_artist(const Track *t)
{
    return t->album_artist[0] ? t->album_artist : t->artist;
}

// Orders track indices so that the members of one album are adjacent, in disc
// and track order. An album is identified by its title and album artist.
static int cmp_track_album(const void *pa, const void *pb)
{
    int ia = *(const int *)pa;
    int ib = *(const int *)pb;
    const Track *a = &g_lib.tracks[ia];
    const Track *b = &g_lib.tracks[ib];

    int c = util_cmp_icase(a->album, b->album);
    if (c)
        return c;
    c = util_cmp_icase(album_group_artist(a), album_group_artist(b));
    if (c)
        return c;
    if (a->disc_no != b->disc_no)
        return (int)a->disc_no - (int)b->disc_no;
    if (a->track_no != b->track_no)
        return (int)a->track_no - (int)b->track_no;
    c = util_cmp_icase(a->title, b->title);
    if (c)
        return c;
    return util_cmp_icase(a->path, b->path);
}

static int cmp_album_order(const void *pa, const void *pb)
{
    const Album *a = (const Album *)pa;
    const Album *b = (const Album *)pb;
    int c = util_cmp_icase(a->artist, b->artist);
    if (c)
        return c;
    return util_cmp_icase(a->name, b->name);
}

static Album *album_alloc(void)
{
    if (g_lib.album_count == g_lib.album_capacity) {
        int cap = g_lib.album_capacity ? g_lib.album_capacity * 2 : 64;
        Album *p = (Album *)realloc(g_lib.albums, sizeof(Album) * (size_t)cap);
        if (!p)
            return NULL;
        g_lib.albums = p;
        g_lib.album_capacity = cap;
    }
    Album *a = &g_lib.albums[g_lib.album_count];
    memset(a, 0, sizeof(*a));
    return a;
}

static void albums_free(void)
{
    for (int i = 0; i < g_lib.album_count; i++)
        free(g_lib.albums[i].tracks);
    free(g_lib.albums);
    g_lib.albums = NULL;
    g_lib.album_count = g_lib.album_capacity = 0;
}

void library_build_albums(void)
{
    albums_free();
    if (g_lib.track_count <= 0)
        return;

    int *idx = (int *)malloc(sizeof(int) * (size_t)g_lib.track_count);
    if (!idx)
        return;
    for (int i = 0; i < g_lib.track_count; i++)
        idx[i] = i;
    qsort(idx, (size_t)g_lib.track_count, sizeof(int), cmp_track_album);

    for (int i = 0; i < g_lib.track_count;) {
        const Track *first = &g_lib.tracks[idx[i]];
        int j = i + 1;
        while (j < g_lib.track_count) {
            const Track *other = &g_lib.tracks[idx[j]];
            if (util_cmp_icase(first->album, other->album) ||
                util_cmp_icase(album_group_artist(first), album_group_artist(other)))
                break;
            j++;
        }

        int count = j - i;
        int *tracks = (int *)malloc(sizeof(int) * (size_t)count);
        if (!tracks)
            break;
        memcpy(tracks, idx + i, sizeof(int) * (size_t)count);

        Album *album = album_alloc();
        if (!album) {
            free(tracks);
            break;
        }
        util_copy(album->name, sizeof(album->name), first->album);
        util_copy(album->artist, sizeof(album->artist), album_group_artist(first));
        album->tracks = tracks;
        album->count = count;
        g_lib.album_count++;
        i = j;
    }

    free(idx);

    if (g_lib.album_count > 1)
        qsort(g_lib.albums, (size_t)g_lib.album_count, sizeof(Album), cmp_album_order);
}

int library_album_of_track(int track)
{
    if (track < 0)
        return -1;
    for (int i = 0; i < g_lib.album_count; i++) {
        for (int k = 0; k < g_lib.albums[i].count; k++) {
            if (g_lib.albums[i].tracks[k] == track)
                return i;
        }
    }
    return -1;
}

// Matches on the file name alone, for playlists written on another machine or
// saved in the wrong folder. The folder name breaks ties between duplicates.
static int find_by_name(const char *path)
{
    char name[600];
    util_basename(path, name, sizeof(name));
    if (!name[0])
        return -1;

    char dir[600];
    char want_folder[192];
    util_dirname(path, dir, sizeof(dir));
    util_basename(dir, want_folder, sizeof(want_folder));

    int only = -1;
    int matches = 0;

    for (int i = 0; i < g_lib.track_count; i++) {
        char track_name[600];
        util_basename(g_lib.tracks[i].path, track_name, sizeof(track_name));
        if (util_cmp_icase(track_name, name))
            continue;

        char have_folder[192];
        util_basename(g_lib.tracks[i].folder, have_folder, sizeof(have_folder));
        if (want_folder[0] && util_cmp_icase(have_folder, want_folder) == 0)
            return i;

        only = i;
        matches++;
    }

    return matches == 1 ? only : -1;
}

static int find_exact(const char *normalized)
{
    for (int i = 0; i < g_lib.track_count; i++) {
        if (util_cmp_icase(g_lib.tracks[i].path, normalized) == 0)
            return i;
    }
    return -1;
}

int library_find_exact(const char *path)
{
    char normalized[600];
    util_join_normalized(normalized, sizeof(normalized), NULL, path);
    return find_exact(normalized);
}

int library_find_path(const char *path)
{
    char normalized[600];
    util_join_normalized(normalized, sizeof(normalized), NULL, path);

    int idx = find_exact(normalized);
    return idx >= 0 ? idx : find_by_name(normalized);
}

void library_free(void)
{
    free(g_lib.tracks);
    g_lib.tracks = NULL;
    g_lib.track_count = g_lib.track_capacity = 0;

    albums_free();

    for (int i = 0; i < g_lib.playlist_count; i++)
        free(g_lib.playlists[i].tracks);
    free(g_lib.playlists);
    g_lib.playlists = NULL;
    g_lib.playlist_count = g_lib.playlist_capacity = 0;
}
