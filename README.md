# Crystal Player

An MP3 player homebrew for the Nintendo 3DS.

It reads everything from the SD card, so there is no database to set up: drop
your music into `sdmc:/music`, run the app, and it groups your songs into
albums, shows album art from the files' own tags, and plays. M3U8 playlists are
imported automatically, and music keeps playing when the 3DS lid is closed.

## Download

[![Download MusicPlayer3DS.cia](docs/download-button.svg)](https://github.com/paige-morningstar/crystal-player/releases/latest/download/MusicPlayer3DS.cia)

One click, no account: that button always serves the newest release's
`MusicPlayer3DS.cia`.

### Or install it from the console

Point **FBI → Remote Install → Scan QR Code** at this, and the 3DS downloads and
installs the same `.cia` over Wi-Fi:

<img src="docs/qr-cia.png" alt="QR code linking to the latest Crystal Player .cia" width="240">

The code resolves
to `https://github.com/paige-morningstar/crystal-player/releases/latest/download/MusicPlayer3DS.cia`,
so it always fetches the newest release rather than a frozen one. If your camera
struggles with the size, hold the console ~15 cm away and fill the frame.

## Features

- **Albums, not one long list.** Scans `sdmc:/music` (and every sub folder) for
  `.mp3` files and groups them into albums, so the library opens as a list of
  albums. Opening one lists its songs in disc and track order. Albums are keyed
  by the album artist tag when a file has one, so a compilation stays a single
  album instead of splitting into one per track artist. Files with no album tag
  use their folder name.
- **Tags.** Reads ID3v2.2 / v2.3 / v2.4 and ID3v1: title, artist, album, year,
  track and disc numbers. Missing tags fall back to the file name and folder
  name.
- **Album art from the tags.** The cover picture embedded in the MP3 (the
  APIC frame, JPEG or PNG) is decoded and shown on the top screen, centre
  cropped and cached per album.
- **Playlists.** Every `.m3u8` / `.m3u` file inside `sdmc:/music` (any sub
  folder) becomes a playlist. `#EXTINF` lines are accepted, though the song's own
  title is what gets shown, not the playlist's; relative paths are resolved from
  the playlist's own folder, and Windows backslash paths are handled.
- **Plays with the lid closed.** Sleep mode is disabled while the app runs, so
  closing the lid does not pause the music. The console itself switches its
  built-in speakers off whenever the shell is closed, so with the lid shut the
  sound comes out of the headphone jack (headphones, or a speaker cable)
  instead. The screens stay powered, so the battery drains faster than in real
  sleep mode - that is the unavoidable trade for keeping the DSP running.
- **Queue playback, shuffle and repeat.** Starting a song queues the whole
  list, so a track automatically continues into the next one. `ZL` / `ZR` skip
  around the queue, `SELECT` + `Y` shuffles what is left to play, and
  `SELECT` + `X` cycles repeat off / all / one. The active modes are shown in
  the bottom right of the top screen.
- **Volume.** `SELECT` + d-pad up / down changes the level in the app in steps
  of about 6%, shown both in the status line and as `VOL 68%` on the top screen
  whenever it is not at full.

## How it works

Everything happens on the console, from the SD card, with no database.

- **Scan** (`library.c`). On boot (and on `Y`) the app walks `sdmc:/music`
  recursively, up to 12 folders deep. Every `.mp3` is read for tags
  (`id3.c`: ID3v2.2/2.3/2.4 and ID3v1) and probed for its audio start, bit rate
  and sample rate (`mp3info.c`, which skips large ID3v2 tags and trusts a
  Xing/Info frame's count when there is one). Every `.m3u8` / `.m3u` becomes a
  playlist. Files and folders whose names start with a dot are skipped, which is
  what keeps Finder's `._` droppings out of your library.
- **Albums** (`library_build_albums`). Tracks are grouped by album tag, with the
  album artist tag (or the track artist when there is none) deciding which album
  a file belongs to, then sorted by disc and track number. No album tag means
  the folder name is the album.
- **Playlists**. Each entry is resolved against the tracks found in the scan:
  exact path first, then by file name with the folder breaking ties. A playlist
  therefore survives being written with paths from another machine, and entries
  that match nothing are counted as "missing" rather than breaking it.
- **Playback** (`player.c`). The current queue is decoded by dr_mp3 in chunks,
  resampled in software to the DSP's 32728 Hz (it runs at `SYSCLOCK_SOC / 512`
  and glitches on anything higher), and streamed through four NDSP wave buffers
  of ~125 ms that are refilled as they are consumed. Starting any song queues
  the list it came from, so play continues into the next track.
- **Album art** (`art.c`). The APIC picture is decoded with stb_image, centre
  cropped to a square, box-filtered down, tiled into the 3DS texture layout,
  uploaded once per album and cached; citro2d then draws it from the cache.
- **Rendering** (`ui.c`, `main.c`). citro2d text and shapes on both screens,
  driven by a small state machine: album list, album contents, playlist list,
  playlist contents. `aptSetSleepAllowed(false)` is set at startup so closing
  the lid does not stop playback.

## Install

Two builds are produced from the same source. Both read the same
`sdmc:/music`, so you can use either (or both).

### Homebrew Launcher (.3dsx)

Copy `MusicPlayer3DS.3dsx` to your SD card:

```
/3ds/MusicPlayer3DS.3dsx
```

Then start it from the Homebrew Launcher. The `.smdh` is embedded, so the app
shows up with its title and icon.

### Home Menu app (.cia)

`MusicPlayer3DS.cia` installs as a normal title, so it launches straight from
the Home Menu without the Homebrew Launcher. Copy the file anywhere on the SD
card and install it with FBI, or use the [QR code](#or-install-it-from-the-console)
above to install it without copying anything:

```
MusicPlayer3DS.cia      title id 000400000FF21C00
```

The `.cia` is signed with makerom's placeholder signature, so it needs
signature checks patched - that is the default for Luma3DS and every other
current 3DS CFW. It will not install on a stock, unmodified console.

To remove it later, delete the title in FBI → Titles (or *System Settings →
Data Management*); it stores nothing but the app itself.

## Music layout

```
sdmc:/music/
├── Daft Punk - Discovery/
│   ├── 01 One More Time.mp3
│   └── 02 Aerodynamic.mp3
├── Radiohead - In Rainbows/
│   └── 01 15 Step.mp3
├── Single tracks/
│   └── Some Loose Song.mp3
└── Roadtrip.m3u8
```

Files directly in `sdmc:/music` work too - they land under "Unknown Album".

## Playlists

Any `.m3u8` or `.m3u` file under `sdmc:/music` (in any sub folder) is picked up
on the next scan and listed as a playlist. They are plain text, one file per
line:

```
#EXTM3U
#EXTINF:224,Daft Punk - One More Time
Daft Punk - Discovery\01 One More Time.mp3
#EXTINF:301,Aerodynamic
../Electro/Aerodynamic.mp3
sdmc:/music/Radiohead - In Rainbows/01 15 Step.mp3
```

- Lines starting with `#` are ignored. `#EXTINF:` titles are accepted but not
  shown: every song is listed by its own title tag.
- Entries that are relative are resolved against the playlist's folder, and an
  entry whose path does not match anything is retried by file name (the folder
  name breaks ties between duplicates), so playlists exported by other music
  apps usually work once the file names line up;
  `..` and `.` work.
- Entries with a device prefix (`sdmc:/...`) or a drive letter
  (`C:\Music\...`) are used as-is (the drive letter is meaningless on a 3DS,
  so such entries are usually skipped if they point outside your library).
- Files that are not in the library are counted as "missing" on the playlist
  row instead of breaking the playlist.

Put playlists anywhere under `sdmc:/music`. Press `SELECT` to switch between
the album list and the playlist list, and `Y` to rescan the card after adding
new files.

### Converting a playlist from the Music app

Music exports absolute paths to your Mac, which mean nothing on the console.
`tools/apple_music_to_m3u8.py` rewrites them to the files on the card:

```sh
# File > Library > Export Playlist -> M3U8 in Music first, then:
python3 tools/apple_music_to_m3u8.py ~/Desktop/Chill.m3u8 /Volumes/3DS/music
```

It matches entries by file name (then by name without the extension, so a
playlist of `.m4a` files still lines up with `.mp3` copies), breaks ties between
same-named files using the folder, keeps the playlist's order, writes
`Chill.m3u8` into the music folder with relative paths, and lists any entries it
could not find. Needs nothing but Python 3.

## Controls

Top screen shows the current song, album art, progress and what plays next.
The lower screen is the list.

| Button | Action |
| --- | --- |
| D-pad up / down | Move through the list |
| `L` / `R` | Page up / down |
| `A` | Open the selected album or playlist, or play the selected song |
| `B` | Back out of an album or playlist |
| `X` | Play / pause |
| `ZL` / `ZR` | Previous / next song |
| D-pad left / right | Seek back / forward 5 s (hold to keep seeking) |
| `Y` | Rescan the SD card |
| `SELECT` (tap) | Switch between albums and playlists |
| `SELECT` + d-pad up / down | Volume up / down |
| `SELECT` + `Y` | Shuffle on / off |
| `SELECT` + `X` | Repeat: off -> all -> one |
| `START` | Quit to the Homebrew Launcher |

`ZL` restarts the current song if you are more than three seconds into it,
just like a normal music player.

## Format support

MP3 only (MPEG-1 / MPEG-2 / MPEG-2.5 Layer III, mono or stereo, any bit rate or
sample rate up to 48 kHz). Decoding is done in software and resampled to the
DSP's native 32728 Hz output, so 44.1 and 48 kHz files play at the right pitch.

Other formats (AAC, FLAC, OGG) are not supported.

## Building

You need [devkitPro](https://devkitpro.org/wiki/Getting_Started) with the 3DS
toolchain and libraries:

```sh
export DEVKITPRO=/opt/devkitpro
make        # MusicPlayer3DS.3dsx + .smdh
make cia    # MusicPlayer3DS.cia
make test   # host-side tests, no 3DS needed (albums, playlists, converter)
```

`make` also rebuilds `MusicPlayer3DS.smdh` from `resources/icon.png`.
`make clean` removes all build output.

Packaging the CIA needs [makerom](https://github.com/3DSGuy/Project_CTR).
Download a build and put `makerom` on your `PATH`, or drop the binary in
`tools/`, which the Makefile prefers. It is deliberately not committed here:
makerom is GPL-2.0, and this repository does not redistribute it. The title,
permissions and service access list it packages are in `resources/crystal.rsf`.

The banner is committed as `resources/banner.bnr`, so packaging needs only
makerom. To change the artwork or the chime:

```sh
python3 tools/make_assets.py                 # resources/icon.png, banner.png, banner.wav
bannertool makebanner -i resources/banner.png -a resources/banner.wav \
    -o resources/banner.bnr
```

[bannertool](https://github.com/Epicpkmn11/bannertool) builds the `.bnr` (it
needs a 256x128 PNG plus a WAV; silent banners are not allowed).

The build is a single-pass makefile (no recursive make), so it also works from
folders whose names contain spaces.

## Project layout

```
source/              player sources (library, id3, mp3info, m3u8, player, art, ui, main)
tests/               host-side tests: album grouping, playlist matching, converter
tests/fixtures/      small music folder and an Apple-style export used by them
vendor/              dr_mp3.h, stb_image.h
resources/crystal.rsf  title id, permissions and services for the .cia
resources/banner.bnr   Home Menu banner (picture + chime) packaged into the .cia
resources/banner.png   banner artwork source
resources/banner.wav   banner tune source
resources/icon.png     app icon (SMDH + .cia)
tools/makerom          optional makerom copy, not committed (see Building)
tools/make_assets.py   regenerates icon.png, banner.png and banner.wav
tools/apple_music_to_m3u8.py  rewrites a Music app playlist for the 3DS
```

## Third-party code

- [dr_mp3](https://github.com/mackron/dr_libs) (public domain / MIT-0) - MP3
  decoding.
- [stb_image](https://github.com/nothings/stb) (public domain) - JPEG/PNG cover
  art decoding.
- [makerom](https://github.com/3DSGuy/Project_CTR) (GPL-2.0) - packages the
  `.cia`. Used, not redistributed here; get it from that link.
- [bannertool](https://github.com/Epicpkmn11/bannertool) (MIT) - made
  `resources/banner.bnr`; it is not needed to build.

The two decoders are vendored in `vendor/`.

## License

Crystal Player is released under the [MIT license](LICENSE). That covers the
player's own code in `source/`, `tests/` and `tools/`.

The vendored decoders keep their own terms and notices, unchanged in `vendor/`:
dr_mp3 is public domain / MIT-0, stb_image is public domain. makerom is GPL-2.0,
is not part of this repository, and is not covered by the MIT license.
