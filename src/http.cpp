#include "http.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include <curl/curl.h>

#include <array>
#include <cstring>
#include <mutex>



namespace tsuzuki::http {
namespace {

// Pulls the two headers worth knowing about out of the response.
size_t headerCb(char* buffer, size_t size, size_t items, void* userdata) {
    const size_t bytes = size * items;
    auto* r = static_cast<Response*>(userdata);
    std::string line(buffer, bytes);

    const auto colon = line.find(':');
    if (colon == std::string::npos) return bytes;
    std::string name = line.substr(0, colon);
    std::string value = line.substr(colon + 1);

    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.erase(0, 1);
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) value.pop_back();

    if (name == "x-ratelimit-remaining") {
        r->rateLimitRemaining = std::atoi(value.c_str());
    } else if (name == "retry-after") {
        r->retryAfterSeconds = std::atoll(value.c_str());
    }
    return bytes;
}


std::once_flag g_initOnce;
std::string g_dohUrl;
std::mutex g_dohMutex;

void applyDoh(CURL* curl) {
    std::lock_guard<std::mutex> lock(g_dohMutex);
    if (!g_dohUrl.empty()) curl_easy_setopt(curl, CURLOPT_DOH_URL, g_dohUrl.c_str());
}

void globalInit() {
    std::call_once(g_initOnce, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

std::size_t writeCb(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

constexpr char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

}  // namespace

Response get(const std::string& url, int timeoutSeconds) {
    globalInit();

    Response r;
    CURL* curl = curl_easy_init();
    if (!curl) {
        r.error = "curl_easy_init failed";
        return r;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeoutSeconds));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "tsuzuki/0.1 (+https://github.com/RealNumNums/Tsuzuki)");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &r.body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &r);
    applyDoh(curl);

    const CURLcode code = curl_easy_perform(curl);
    if (code == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &r.status);
        r.ok = (r.status >= 200 && r.status < 300);
        if (!r.ok) r.error = "HTTP " + std::to_string(r.status);
    } else {
        r.error = curl_easy_strerror(code);
    }

    curl_easy_cleanup(curl);
    return r;
}


Response postJson(const std::string& url, const std::string& body, int timeoutSeconds,
                  const std::string& bearerToken) {
    globalInit();

    Response r;
    CURL* curl = curl_easy_init();
    if (!curl) {
        r.error = "curl_easy_init failed";
        return r;
    }

    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    std::string auth;
    if (!bearerToken.empty()) {
        auth = "Authorization: Bearer " + bearerToken;
        headers = curl_slist_append(headers, auth.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeoutSeconds));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "tsuzuki/0.1 (+https://github.com/RealNumNums/Tsuzuki)");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &r.body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &r);
    applyDoh(curl);

    const CURLcode code = curl_easy_perform(curl);
    if (code == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &r.status);
        r.ok = (r.status >= 200 && r.status < 300);
        if (!r.ok) r.error = "HTTP " + std::to_string(r.status);
    } else {
        r.error = curl_easy_strerror(code);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return r;
}

void setDohUrl(const std::string& url) {
    std::lock_guard<std::mutex> lock(g_dohMutex);
    g_dohUrl = url;
}

std::string urlEncode(const std::string& s) {
    globalInit();
    CURL* curl = curl_easy_init();
    if (!curl) return s;
    char* enc = curl_easy_escape(curl, s.c_str(), static_cast<int>(s.size()));
    std::string out = enc ? enc : s;
    if (enc) curl_free(enc);
    curl_easy_cleanup(curl);
    return out;
}

std::string base64Decode(const std::string& s) {
    std::array<int, 256> rev{};
    rev.fill(-1);
    for (int i = 0; i < 64; ++i) rev[static_cast<unsigned char>(kBase64Alphabet[i])] = i;

    std::string out;
    int buffer = 0;
    int bits = 0;
    for (const unsigned char c : s) {
        if (c == '=') break;
        const int v = rev[c];
        if (v < 0) continue;  // skip whitespace / padding noise
        buffer = (buffer << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((buffer >> bits) & 0xFF));
        }
    }
    return out;
}

}  // namespace tsuzuki::http
