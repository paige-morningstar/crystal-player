#!/usr/bin/env python3
"""Convert a playlist exported from the macOS Music app for Crystal Player.

    python3 tools/apple_music_to_m3u8.py Chill.m3u8 /Volumes/3DS/music

Music exports a playlist as a list of absolute paths to your Mac's files:

    /Users/you/Music/Media.localized/Music/Christopher Larkin/02 Dirtmouth.m4a

which mean nothing on a 3DS. This rewrites each entry to the name of the matching
file under the music folder on the SD card, written relative to the playlist, and
saves the result there as <name>.m3u8 so the player picks it up on the next scan.

Entries are matched by file name, then by name without the extension, so a
playlist of .m4a files still lines up with .mp3 copies. Anything that cannot be
matched is left out and listed at the end. Pure stdlib.
"""

import argparse
import os
import sys
import unicodedata

AUDIO_EXT = ".mp3"  # the only format Crystal Player decodes


def key(name):
    """Comparison key: macOS and FAT cards disagree about Unicode normalisation."""
    return unicodedata.normalize("NFC", name).casefold()


def read_entries(path):
    """Path lines of an M3U/M3U8 file, comments and blanks dropped."""
    entries = []
    with open(path, "r", encoding="utf-8-sig", errors="replace") as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#"):
                entries.append(line)
    return entries


def index_music(root):
    """Every playable file under root, keyed by file name and by name sans extension."""
    by_name = {}
    by_stem = {}

    for dirpath, dirnames, filenames in os.walk(root):
        dirnames.sort()
        for name in sorted(filenames):
            if not name.lower().endswith(AUDIO_EXT):
                continue
            full = os.path.join(dirpath, name)
            stem = os.path.splitext(name)[0]
            by_name.setdefault(key(name), []).append(full)
            by_stem.setdefault(key(stem), []).append(full)

    return by_name, by_stem


def match(entry, by_name, by_stem):
    """The SD card file for one playlist entry, or None."""
    parts = entry.replace("\\", "/")
    name = key(os.path.basename(parts))
    stem = key(os.path.splitext(name)[0])

    candidates = by_name.get(name) or by_stem.get(stem)
    if not candidates:
        return None
    if len(candidates) == 1:
        return candidates[0]

    # Same file name in several albums: the entry's folder settles it.
    folder = key(os.path.basename(os.path.dirname(parts)))
    for candidate in candidates:
        if key(os.path.basename(os.path.dirname(candidate))) == folder:
            return candidate

    return candidates[0]


def main():
    parser = argparse.ArgumentParser(
        description="Rewrite a Music app playlist export for the 3DS player.")
    parser.add_argument("playlist", help="the .m3u8 / .m3u exported from Music")
    parser.add_argument("music", help="the music folder on the SD card")
    parser.add_argument("-o", "--output",
                        help="where to write the playlist (default: inside the music folder)")
    args = parser.parse_args()

    root = os.path.abspath(args.music)
    if not os.path.isdir(root):
        sys.exit("%s is not a folder" % args.music)

    try:
        entries = read_entries(args.playlist)
    except OSError as exc:
        sys.exit("cannot read %s: %s" % (args.playlist, exc))

    output = args.output or os.path.join(
        root, os.path.splitext(os.path.basename(args.playlist))[0] + ".m3u8")
    output_dir = os.path.dirname(os.path.abspath(output))

    by_name, by_stem = index_music(root)
    if not by_name:
        sys.exit("no %s files under %s" % (AUDIO_EXT, args.music))

    lines = ["#EXTM3U"]
    missing = []
    for entry in entries:
        found = match(entry, by_name, by_stem)
        if not found:
            missing.append(entry)
            continue
        lines.append(os.path.relpath(found, output_dir).replace(os.sep, "/"))

    if len(lines) == 1:
        sys.exit("none of the %d entries matched a file under %s" % (len(entries), args.music))

    try:
        with open(output, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(lines) + "\n")
    except OSError as exc:
        sys.exit("cannot write %s: %s" % (output, exc))

    print("wrote %s (%d songs)" % (output, len(lines) - 1))
    if missing:
        print("%d entries not found on the card:" % len(missing))
        for entry in missing[:20]:
            print("  " + entry)
        if len(missing) > 20:
            print("  ... and %d more" % (len(missing) - 20))


if __name__ == "__main__":
    main()
