// See stream.hpp for why the player is handed a URL rather than a path.

#include "stream.hpp"

#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_info.hpp>

#include <httplib.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>

namespace tsuzuki::stream {
namespace {

namespace lt = libtorrent;

// How far past the current read to keep asking for. Enough that sequential
// reading never has to wait, small enough that a seek does not leave a long
// tail of now-pointless requests at the front of the queue.
constexpr int kReadaheadPieces = 24;

// Deadlines are handed out in ascending order so libtorrent fetches the piece
// we are about to need before the one after it.
constexpr int kDeadlineStepMs = 200;

struct Current {
    lt::torrent_handle handle;
    std::shared_ptr<const lt::torrent_info> info;
    int fileIndex = -1;
    std::int64_t fileSize = 0;
    std::string path;  // absolute, on disk
    // Bumped every time the served file changes, so a request still running
    // for the previous episode gives up instead of fighting the new one.
    int generation = 0;
};

std::mutex g_mutex;
Current g_current;
std::atomic<int> g_generation{0};
std::atomic<bool> g_alive{true};

std::unique_ptr<httplib::Server> g_server;
std::thread g_thread;
int g_port = 0;

Current snapshot() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_current;
}

// The piece holding a byte offset within the file.
int pieceAt(const Current& c, std::int64_t offset) {
    const lt::peer_request r =
        c.info->map_file(lt::file_index_t{c.fileIndex}, offset, 0);
    return static_cast<int>(r.piece);
}

// Ask for a piece and wait for it. False means the caller should give up:
// either the stream was replaced or we are shutting down.
bool awaitPiece(const Current& c, int piece, int generation) {
    if (c.handle.have_piece(lt::piece_index_t{piece})) return true;

    // Re-asserted rather than set once. A file_priority() update applied on
    // libtorrent's thread recomputes the whole file's piece priorities, which
    // silently discards a deadline set from here - and a discarded deadline on
    // the piece a read is blocked on is a wait that never ends.
    int sinceAssert = 0;
    while (g_alive.load() && g_generation.load() == generation) {
        if (sinceAssert == 0) {
            c.handle.piece_priority(lt::piece_index_t{piece}, lt::top_priority);
            c.handle.set_piece_deadline(lt::piece_index_t{piece}, 0);
        }
        if (c.handle.have_piece(lt::piece_index_t{piece})) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        sinceAssert = (sinceAssert + 1) % 25;  // roughly once a second
    }
    return false;
}

// Keep the pieces after `piece` moving, so sequential reading does not stop at
// every piece boundary to wait for the next one.
void requestAhead(const Current& c, int piece) {
    const int last = pieceAt(c, (std::max)(c.fileSize - 1, std::int64_t{0}));
    for (int i = 1; i <= kReadaheadPieces; ++i) {
        const int p = piece + i;
        if (p > last) break;
        if (c.handle.have_piece(lt::piece_index_t{p})) continue;
        c.handle.piece_priority(lt::piece_index_t{p}, lt::top_priority);
        c.handle.set_piece_deadline(lt::piece_index_t{p}, i * kDeadlineStepMs);
    }
}

// Reads `length` bytes at `offset`, waiting for whatever has not arrived.
// Returns false if the stream was replaced underneath us or the read failed.
bool readRange(std::int64_t offset, std::size_t length, char* out, int generation) {
    const Current c = snapshot();
    if (c.fileIndex < 0 || !c.info) return false;

    std::ifstream in(c.path, std::ios::binary);
    if (!in) return false;

    std::size_t done = 0;
    while (done < length) {
        if (!g_alive.load() || g_generation.load() != generation) return false;

        const std::int64_t at = offset + static_cast<std::int64_t>(done);
        const int piece = pieceAt(c, at);
        if (!awaitPiece(c, piece, generation)) return false;
        requestAhead(c, piece);

        // Never read past the end of the piece we just waited for; the next
        // one round the loop is a separate wait.
        const std::int64_t pieceEnd =
            static_cast<std::int64_t>(piece + 1) * c.info->piece_length();
        const lt::peer_request base =
            c.info->map_file(lt::file_index_t{c.fileIndex}, 0, 0);
        const std::int64_t fileStartAbs =
            static_cast<std::int64_t>(static_cast<int>(base.piece)) * c.info->piece_length() +
            base.start;
        const std::int64_t chunkEndInFile = pieceEnd - fileStartAbs;

        std::size_t take = length - done;
        if (chunkEndInFile > at) {
            take = (std::min)(take, static_cast<std::size_t>(chunkEndInFile - at));
        }

        in.seekg(at, std::ios::beg);
        if (!in) return false;
        in.read(out + done, static_cast<std::streamsize>(take));
        const std::streamsize got = in.gcount();
        if (got <= 0) return false;
        done += static_cast<std::size_t>(got);
    }
    return true;
}

void installRoute() {
    g_server->Get("/stream", [](const httplib::Request&, httplib::Response& res) {
        const Current c = snapshot();
        if (c.fileIndex < 0 || !c.info) {
            res.status = 404;
            return;
        }
        const int generation = g_generation.load();

        // httplib parses Range itself and calls the provider with the offsets
        // the client asked for, as long as it is told the total length.
        res.set_content_provider(
            static_cast<std::size_t>(c.fileSize), "video/x-matroska",
            [generation](std::size_t offset, std::size_t length, httplib::DataSink& sink) {
                // One piece-ish at a time, so a cancelled request notices
                // quickly rather than after the whole range.
                constexpr std::size_t kChunk = 256 * 1024;
                const std::size_t take = (std::min)(length, kChunk);

                std::string buf(take, '\0');
                if (!readRange(static_cast<std::int64_t>(offset), take, buf.data(), generation)) {
                    return false;
                }
                return sink.write(buf.data(), buf.size());
            });
    });
}

}  // namespace

std::string serve(const lt::torrent_handle& handle,
                  std::shared_ptr<const lt::torrent_info> info, int fileIndex,
                  const std::string& savePath) {
    if (!info || fileIndex < 0) return {};

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_current.handle = handle;
        g_current.info = info;
        g_current.fileIndex = fileIndex;
        g_current.fileSize = info->layout().file_size(lt::file_index_t{fileIndex});
        g_current.path = savePath + "/" + info->layout().file_path(lt::file_index_t{fileIndex});
    }
    // Anything still serving the previous file stops now.
    ++g_generation;

    if (!g_server) {
        g_server = std::make_unique<httplib::Server>();

        // Waiting is the whole point of this server, and httplib's default
        // five-second write timeout treats a wait as a dead client: it closed
        // the connection mid-file, the player reported the stream ending
        // early, and reconnected with a backoff that grew to seconds. A read
        // that has to wait for a piece on a slow swarm can legitimately take
        // much longer than five seconds.
        g_server->set_write_timeout(std::chrono::minutes(10));
        g_server->set_read_timeout(std::chrono::seconds(30));

        installRoute();
        g_port = g_server->bind_to_any_port("127.0.0.1");
        if (g_port <= 0) {
            g_server.reset();
            return {};
        }
        g_thread = std::thread([] { g_server->listen_after_bind(); });
    }

    return "http://127.0.0.1:" + std::to_string(g_port) + "/stream";
}

void stop() {
    ++g_generation;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_current.fileIndex = -1;
    g_current.info.reset();
}

void shutdown() {
    g_alive.store(false);
    ++g_generation;
    if (g_server) g_server->stop();
    if (g_thread.joinable()) g_thread.join();
    g_server.reset();
}

}  // namespace tsuzuki::stream
