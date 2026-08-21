#include "http.hpp"

#include <curl/curl.h>

#include <array>
#include <cstring>
#include <mutex>

namespace tsuzuki::http {
namespace {

std::once_flag g_initOnce;

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

Response postJson(const std::string& url, const std::string& body, int timeoutSeconds) {
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
