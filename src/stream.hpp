#pragma once

// A local HTTP server that hands one file of a torrent to the player.
//
// The player used to be given the path of the partially-downloaded file
// directly, and that is the source of a whole class of bug. A read of a region
// that has not arrived does not fail and does not wait - the filesystem hands
// back zeros. The player cannot tell those zeros from the file's real content,
// so it reports corruption, resynchronises forward looking for a valid frame,
// and walks off into the rest of the file. What the viewer sees is the episode
// skipping.
//
// No amount of reading further ahead fixes it, because the player does not
// only read ahead. A container's index, chapters and attachments can sit
// anywhere in the file, and the player will go and read them wherever they
// are, long before playback reaches that point.
//
// Serving the file over HTTP makes the read block instead. A range request for
// bytes that have not arrived raises their priority and waits for them, so the
// player is never handed anything but real data - it just waits sometimes.
//
// It also puts the piece priorities where they belong. They used to follow the
// playback position, which is a guess at what the player will read next. Here
// they follow the reads themselves, which is not a guess.

// libtorrent's own forward declarations: hand-rolling them lands in the wrong
// namespace, because the real types live in an inline ABI namespace.
#include <libtorrent/fwd.hpp>

#include <memory>
#include <string>

namespace tsuzuki::stream {

// Starts serving `fileIndex` of `handle` and returns the URL to give the
// player. Replaces whatever was being served before. Empty on failure.
std::string serve(const libtorrent::torrent_handle& handle,
                  std::shared_ptr<const libtorrent::torrent_info> info, int fileIndex,
                  const std::string& savePath);

// Cancels any in-flight reads and stops serving. Safe to call when idle.
void stop();

void shutdown();

}  // namespace tsuzuki::stream
