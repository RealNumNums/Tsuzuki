#pragma once

#include <string>

namespace tsuzuki::ui {

// Serves the interface on 127.0.0.1:<port> and opens it in the default
// browser. Blocks until the server is stopped. Returns a process exit code.
//
// The UI is HTML/CSS rather than a native toolkit: it keeps the binary small,
// avoids a multi-hour Qt build, and lets the interface actually look like
// something. The engine underneath is the same code the CLI uses.
int run(int port, const std::string& savePath);

}  // namespace tsuzuki::ui
