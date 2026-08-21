<img src="assets/logo.svg" width="96" align="left" alt="Tsuzuki logo">

# Tsuzuki (続き)

*"To be continued."* A native anime torrent streaming client.

<br clear="left">

<img src="assets/mascot.svg" width="150" align="right" alt="Tsuzuki mascot">

Named for the card at the end of an episode, because the thing that started
this project was a client that kept playing the wrong continuation.

## Why

Built after digging into why an existing client kept playing the wrong episode
out of batch torrents. The cause turned out to be a fallback chain that
silently substitutes a different file when episode matching fails:

```ts
// hayase-app/interface  src/lib/components/ui/player/resolver.ts:63
const targetEpisode =
  targetAnimeFiles.find(f => f.metadata.episode === list.episode)
  ?? targetAnimeFiles.find(f => f.metadata.episode === 1)
  ?? targetAnimeFiles[0]
  ?? resolvedFiles[0]
```

`metadata.episode` is `string | number | undefined` and multi-episode files get
a range *string* (`"1 ~ 12"`), so `===` against a number fails, and you get
episode 1 — or, because the sort directly above it orders seasons *descending*,
season 3 episode 1 of a multi-season pack.

**Tsuzuki's rule: never play a file the user did not ask for.** A failed match
is reported, not papered over.

## Goals

1. **Filter properly** — episode/season filtering that understands real release names
2. **Know what's actually playing** — show the file→episode mapping, let it be corrected
3. **Delete when done** — verified cleanup, not best-effort
4. **Be smarter than the original** — mostly means: never guess silently

## Stack

| | |
|---|---|
| Torrent | libtorrent-rasterbar |
| Filename parsing | Anitomy (native C++, vendored via FetchContent) |
| HTTP / JSON / XML | libcurl, nlohmann-json, pugixml |
| Playback | mpv subprocess now; libmpv + libass later |
| Build | CMake + vcpkg manifest mode, MSVC |

## Roadmap

- [x] **M0** — scaffold, build config
- [ ] **M1** — magnet → file/episode table → pick → sequential stream → mpv
- [ ] **M2** — native torrent sources (Nyaa, SeaDex, AnimeTosho, nekoBT, SubsPlease)
- [ ] **M3** — AniList metadata; mappings cached by `(infohash, file_index)` and user-correctable
- [ ] **M4** — verified delete-on-exit
- [ ] **M5** — GUI, embedded libmpv, libass subtitles

M1 deliberately shells out to `mpv` rather than embedding libmpv: libtorrent
writes to disk and mpv plays a growing file happily, which keeps the first
milestone small enough to actually finish.

## Build

```
cmake --preset default
cmake --build build
```

Requires vcpkg with `VCPKG_ROOT` set; dependencies come from `vcpkg.json`.

## Usage

```
tsuzuki "magnet:?xt=urn:btih:..."              # show the table, pick a file
tsuzuki "magnet:?..." --episode 5              # play episode 5, or fail loudly
tsuzuki "magnet:?..." --episode 5 --keep       # don't clean up afterwards
```

## Licence note

libtorrent and mpv are GPL-family; Anitomy is permissive. That shapes
distribution if binaries are ever published.

Not affiliated with Hayase, and deliberately not sharing its name or branding.
Bring your own content — this ships no sources, indexes or media.
