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

## Talking to mpv

Controls, the rolling window, resume points and progress sync all read the
playhead from mpv's JSON IPC socket, so all four fail together if that socket
is not there.

On Windows mpv **prepends** `\\.\pipe\` to whatever `--input-ipc-server` is
given. Passing the full path makes it listen on `\\.\pipe\\\.\pipe\tsuzuki-mpv`,
which nothing can reach - and mpv reports no error, so playback looks entirely
normal while every control is dead. The option gets the bare name; only the
client side spells out the path.

Because that failure is invisible from the outside, the player now says so on
screen when mpv is running but not answering, rather than quietly skipping the
things that depend on it.

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

Finishing an episode clears its resume point and syncs progress. "Finished" is
hayase-app/interface's rule from `player.svelte` - within `max(180, duration/10)`
of the end - rather than a flat percentage, so skipping the ending still
finishes the episode and a film scales with its runtime instead of demanding
another twenty minutes.

## Accounts

Press **Link AniList**. A sign-in window opens, you approve Tsuzuki, and it
links itself. Episodes are marked watched on the completion rule above.

The mechanism is the one hayase-app/interface uses:

```
authorize?client_id=<id>&response_type=token
```

Implicit grant, and deliberately **no `redirect_uri`**. AniList redirects to
whatever the client is registered with, and the app hosts that navigation in a
WebView2 window it owns, reading the token out of the fragment before the
target page can load. The registered URL therefore never has to be reachable,
or even correct — which removes the client secret, the loopback listener, the
authorization code and the redirect URL from the process entirely.

Passing a `redirect_uri` alongside `response_type=token` is what AniList
answers with `unsupported_grant_type`; that one wrong parameter is why this
took a detour through the authorization code flow first.

The client id lives in gitignored `src/secrets.local.hpp`, so a built binary
carries it and this repository does not. The token is stored in
`%LOCALAPPDATA%\Tsuzuki\auth.json`, and is discarded only when AniList
actually rejects it — never on a timeout.

## Licence note

libtorrent and mpv are GPL-family; Anitomy is permissive. That shapes
distribution if binaries are ever published.

Not affiliated with Hayase, and deliberately not sharing its name or branding.
Bring your own content — this ships no sources, indexes or media.
