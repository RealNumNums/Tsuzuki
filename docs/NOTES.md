# Implementation notes

Things that were not obvious, written down while working them out.

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

## Discord presence

Playing something sets a Discord status of "Watching <show>" with the episode
underneath, updated when you pause and cleared when you stop. Tsuzuki carries
its own Discord application id so this works without any setup; the Settings
field is only there to point the presence at a different application.

The artwork Discord shows is an asset named `tsuzuki` on that application -
`assets/logo-1024.png` is the logo at the size Discord wants, exported from
`assets/logo.svg`.
