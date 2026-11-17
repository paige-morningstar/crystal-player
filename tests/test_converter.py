#!/usr/bin/env python3
"""Tests for tools/apple_music_to_m3u8.py. Run with: make test.

Every case copies tests/fixtures/music into a scratch folder first, so the
converter writes its playlist there instead of into the repository.
"""

import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT = os.path.join(ROOT, "tools", "apple_music_to_m3u8.py")
FIXTURES = os.path.join(ROOT, "tests", "fixtures")

# What the fixture export should turn into. The .m4a entries have to match the
# .mp3 on the card, the two 01 Song.mp3 files have to stay in their own folders,
# and the missing song has to be dropped.
EXPECTED = [
    "#EXTM3U",
    "Hollow Knight/02 Dirtmouth.mp3",
    "Hits/01 Song.mp3",
    "Other/01 Song.mp3",
]

failures = []


def check(what, cond, detail=""):
    if not cond:
        failures.append(what + (" (" + detail + ")" if detail else ""))


def run(*args):
    return subprocess.run([sys.executable, SCRIPT] + list(args),
                          capture_output=True, text=True)


def read_lines(path):
    with open(path, encoding="utf-8") as f:
        return f.read().splitlines()


def case_default_output(tmp, export):
    music = os.path.join(tmp, "music")
    shutil.copytree(os.path.join(FIXTURES, "music"), music)

    result = run(export, music)
    check("exit code", result.returncode == 0, result.stderr.strip())

    written = os.path.join(music, "apple_export.m3u8")
    check("playlist written inside the music folder", os.path.exists(written))
    if os.path.exists(written):
        got = read_lines(written)
        check("entries", got == EXPECTED, repr(got))
        check("missing song dropped", "Missing Song" not in "\n".join(got))
    check("missing song reported", "Missing Song.mp3" in result.stdout)


def case_output_in_subfolder(tmp, export):
    music = os.path.join(tmp, "music")
    shutil.copytree(os.path.join(FIXTURES, "music"), music)
    playlists = os.path.join(music, "playlists")
    os.makedirs(playlists)

    written = os.path.join(playlists, "Chill.m3u8")
    result = run(export, music, "-o", written)
    check("sub folder exit code", result.returncode == 0, result.stderr.strip())
    if os.path.exists(written):
        got = read_lines(written)
        check("sub folder paths", got[1:] == ["../" + p for p in EXPECTED[1:]], repr(got))


def case_errors(tmp, export):
    result = run(export, os.path.join(tmp, "not-a-folder"))
    check("bad music folder fails", result.returncode != 0)
    check("bad music folder message", "not a folder" in result.stderr)

    music = os.path.join(tmp, "music")
    shutil.copytree(os.path.join(FIXTURES, "music"), music)
    empty = os.path.join(tmp, "empty.m3u8")
    with open(empty, "w", encoding="utf-8") as f:
        f.write("#EXTM3U\n/nowhere/gone.mp3\n")
    result = run(empty, music)
    check("no matches fails", result.returncode != 0)
    check("no matches message", "matched" in result.stderr)


def main():
    export = os.path.join(FIXTURES, "apple_export.m3u8")
    if not os.path.isfile(export):
        print("FAIL: missing fixture " + export)
        return 1

    for case in (case_default_output, case_output_in_subfolder, case_errors):
        with tempfile.TemporaryDirectory() as tmp:
            case(tmp, export)

    if failures:
        for failure in failures:
            print("FAIL " + failure)
        print("converter tests: %d FAILED" % len(failures))
        return 1

    print("converter tests: all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
