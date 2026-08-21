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
| UI | Win32 + WebView2 (`Tsuzuki.exe`); loopback server is internal transport only |
| Playback | mpv in-window (--wid), driven over its JSON IPC socket |
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
- [x] **M5** — **native app** (`Tsuzuki.exe`): a Win32 window hosting the
  interface in a WebView2 control, dark title bar, real icon, no browser
  chrome. Same engine in-process; the loopback server is only the transport
  between C++ and the view. Video plays **inside the window** via mpv --wid.

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

Double-click `Tsuzuki.exe` for the app, or use `tsuzuki.cmd` in the repo root
for the console tool. The exes need the DLLs beside them in `build\Release`,
so this is portable as a folder, not as a lone file. Downloads default to
`%TEMP%\tsuzuki` and are removed after playback unless you pass `--keep`.

```
Tsuzuki.exe                                   # native app window
tsuzuki-cli search "frieren" --res 1080            # search every source, pick one
tsuzuki-cli search "frieren" --episode 5           # ...and jump straight to ep 5
tsuzuki-cli "magnet:?xt=urn:btih:..."              # show the table, pick a file
tsuzuki-cli "magnet:?..." --episode 5              # play episode 5, or fail loudly
tsuzuki-cli "magnet:?..." --episode 5 --keep       # don't clean up afterwards
```

## Controls and settings

Playback controls live in a strip under the video: play/pause, seek, scrub,
audio track, subtitle track (including Off), and volume — all driven through
mpv's IPC socket rather than expecting anyone to know its keybindings.

Settings persist to `%LOCALAPPDATA%\Tsuzuki\settings.json`: download folder,
speed limit, max connections, preferred quality, preferred audio/subtitle
language, buffer override, delete-after-watching, subtitles on by default.

## Adaptive streaming

Buffer size is measured, not guessed. Tsuzuki primes the container (head plus
tail), times how fast that arrives, and compares it against the file's bitrate.
Keeping up needs only a short buffer; falling behind pre-loads enough to cover
the shortfall across the whole runtime, because that gap never closes on its
own.

A rolling window then follows the playhead — read from mpv over IPC — keeping
pieces ahead of playback at top priority, with deadlines that tighten as a
piece gets closer to being needed.

## Resuming

Playback position is checkpointed every few seconds to
`%LOCALAPPDATA%\Tsuzuki\progress.json` and passed back to mpv as `--start`.
Ported from hayase-app/interface `watchProgress.ts`, which keeps one entry per
anime - so opening episode 8 forgets where you were in episode 5. This keys on
the episode too, and falls back to the torrent infohash when there is no
AniList id, so plain magnets resume as well.

Finishing an episode (past 90%) clears its resume point and syncs progress.

## Accounts

Tsuzuki links to AniList and marks episodes watched once you pass 80% of them.

AniList rejects the implicit grant (unsupported_grant_type - it is deprecated in
OAuth 2.1 and they have disabled it), so linking uses the authorization code
flow, which does need a client secret.

Build credentials live in `src/secrets.local.hpp`, which is gitignored: a built
binary can carry them so linking is one click, while this repository carries
none. To build your own:

1. Open [anilist.co/settings/developer](https://anilist.co/settings/developer) and create a client
2. Set the redirect URL to `http://127.0.0.1:7654/auth/anilist`
3. Put the id and secret in `src/secrets.local.hpp`, or paste them into
   Settings → Account at runtime.

Linking uses the implicit grant, so no secret is ever stored. The token lives in
`%LOCALAPPDATA%\Tsuzuki\auth.json` and is cleared automatically if AniList
stops accepting it.

## Licence note

libtorrent and mpv are GPL-family; Anitomy is permissive. That shapes
distribution if binaries are ever published.

Not affiliated with Hayase, and deliberately not sharing its name or branding.
Bring your own content — this ships no sources, indexes or media.
