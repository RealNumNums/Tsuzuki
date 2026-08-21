#pragma once

#include <string>

namespace tsuzuki::http {

// Blocking GET. Returns an empty optional-ish result on failure: `ok` false
// with `status` set when the server answered, or 0 when the request never
// completed. Sources are expected to fail soft - one dead index must not take
// down a whole search.
struct Response {
    bool ok = false;
    long status = 0;
    std::string body;
    std::string error;
};

Response get(const std::string& url, int timeoutSeconds = 20);

// POST with a JSON body (AniList's GraphQL endpoint).
Response postJson(const std::string& url, const std::string& body, int timeoutSeconds = 20);

// Percent-encode a query parameter value.
std::string urlEncode(const std::string& s);

// Decode base64 (the extension index stores source URLs base64-encoded).
std::string base64Decode(const std::string& s);

}  // namespace tsuzuki::http
