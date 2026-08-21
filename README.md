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
| UI | HTML served on 127.0.0.1 via cpp-httplib |
| Playback | mpv subprocess now; libmpv + libass later |
| Build | CMake + vcpkg manifest mode, MSVC |

## Roadmap

- [x] **M0** — scaffold, build config
- [x] **M1** — magnet → file/episode table → pick → sequential stream → mpv
  - verified end to end against a public-domain magnet: metadata fetch,
    video filtering, Anitomy mapping, refuse-to-guess (exit 2), head+tail
    buffering, and mpv playback while still downloading
- [x] **M2** — native torrent sources: Nyaa (RSS), AnimeTosho (JSON), SubsPlease
  (JSON), SeaDex (PocketBase, by AniList ID). Merged and deduped by infohash.
  nekoBT is HTML-only with no API and is cross-indexed by AnimeTosho, so it is
  intentionally not implemented.
- [x] **M3** — AniList GraphQL title resolution (canonical title + the ID SeaDex
  is keyed by)
- [x] **M4** — delete-on-exit, verified via `torrent_deleted_alert` **and** a
  filesystem check; reports honestly when cleanup does not happen
- [x] **M5** — browser UI served from the binary (cpp-httplib on 127.0.0.1),
  with the logo and mascot; double-clicking the exe opens it. Native toolkit
  deferred: HTML avoids a multi-hour Qt build and looks better. Embedded
  libmpv + libass still to come.

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

Use `tsuzuki.cmd` in the repo root - the exe needs the DLLs beside it in
`buildRelease`, so it is portable as a folder, not as a lone file. The
launcher defaults `--save-path` to `%TEMP%	suzuki`; downloads are removed
after playback unless you pass `--keep`.

```
tsuzuki ui                                    # open the browser interface
tsuzuki search "frieren" --res 1080            # search every source, pick one
tsuzuki search "frieren" --episode 5           # ...and jump straight to ep 5
tsuzuki "magnet:?xt=urn:btih:..."              # show the table, pick a file
tsuzuki "magnet:?..." --episode 5              # play episode 5, or fail loudly
tsuzuki "magnet:?..." --episode 5 --keep       # don't clean up afterwards
```

## Licence note

libtorrent and mpv are GPL-family; Anitomy is permissive. That shapes
distribution if binaries are ever published.

Not affiliated with Hayase, and deliberately not sharing its name or branding.
Bring your own content — this ships no sources, indexes or media.
