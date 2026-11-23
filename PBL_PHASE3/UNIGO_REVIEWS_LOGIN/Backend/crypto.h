// crypto.h -- the security building blocks for Lost & Found accounts, written from the standards
// (no external libraries):
//   * SHA-256              (FIPS 180-4)  -- a fingerprint of any data, 32 bytes
//   * HMAC-SHA256          (RFC 2104)    -- a keyed fingerprint
//   * PBKDF2-HMAC-SHA256   (RFC 8018)    -- slow, salted password hashing: guessing passwords is expensive
//   * secureRandom()                     -- random bytes from the operating system (salts, session tokens)
//   * hex / base64 helpers, constant-time comparison
// Passwords are never stored: only a random salt and the PBKDF2 result.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
  #include <winsock2.h>
  #include <windows.h>
#endif

namespace unigo {

// ---------- SHA-256 ----------
class Sha256 {
public:
    Sha256() { reset(); }
    void reset() {
        static const uint32_t init[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
        std::memcpy(h_, init, sizeof h_);
        len_ = 0; used_ = 0;
    }
    void update(const void* data, size_t n) {
        const uint8_t* p = (const uint8_t*)data;
        len_ += n;
        while (n > 0) {
            size_t take = std::min(n, (size_t)64 - used_);
            std::memcpy(buf_ + used_, p, take);
            used_ += take; p += take; n -= take;
            if (used_ == 64) { block(buf_); used_ = 0; }
        }
    }
    void update(const std::string& s) { update(s.data(), s.size()); }
    std::string digest() {                                   // 32 raw bytes
        uint64_t bits = len_ * 8;
        uint8_t pad = 0x80;
        update(&pad, 1);
        uint8_t zero = 0;
        while (used_ != 56) update(&zero, 1);
        uint8_t lenBytes[8];
        for (int i = 0; i < 8; ++i) lenBytes[i] = (uint8_t)(bits >> (56 - 8 * i));
        update(lenBytes, 8);
        std::string out(32, '\0');
        for (int i = 0; i < 8; ++i)
            for (int b = 0; b < 4; ++b) out[4 * i + b] = (char)(h_[i] >> (24 - 8 * b));
        reset();
        return out;
    }
    static std::string hash(const std::string& s) { Sha256 h; h.update(s); return h.digest(); }

private:
    static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    void block(const uint8_t* b) {
        static const uint32_t K[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t)b[4 * i] << 24 | (uint32_t)b[4 * i + 1] << 16 | (uint32_t)b[4 * i + 2] << 8 | b[4 * i + 3];
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h_[0], bb = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
            uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & bb) ^ (a & c) ^ (bb & c));
            h = g; g = f; f = e; e = d + t1; d = c; c = bb; bb = a; a = t1 + t2;
        }
        h_[0] += a; h_[1] += bb; h_[2] += c; h_[3] += d; h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += h;
    }
    uint32_t h_[8];
    uint8_t buf_[64];
    uint64_t len_;
    size_t used_;
};

// ---------- HMAC-SHA256: hash(key ^ opad, hash(key ^ ipad, message)) ----------
inline std::string hmacSha256(std::string key, const std::string& msg) {
    if (key.size() > 64) key = Sha256::hash(key);
    key.resize(64, '\0');
    std::string ipad(64, '\0'), opad(64, '\0');
    for (int i = 0; i < 64; ++i) { ipad[i] = (char)(key[i] ^ 0x36); opad[i] = (char)(key[i] ^ 0x5c); }
    return Sha256::hash(opad + Sha256::hash(ipad + msg));
}

// ---------- PBKDF2-HMAC-SHA256 ----------
// Repeats HMAC `iterations` times so that every password guess costs an attacker the same work.
inline std::string pbkdf2Sha256(const std::string& password, const std::string& salt, int iterations, size_t length) {
    std::string out;
    for (uint32_t blockNo = 1; out.size() < length; ++blockNo) {
        std::string be(4, '\0');
        for (int i = 0; i < 4; ++i) be[i] = (char)(blockNo >> (24 - 8 * i));
        std::string u = hmacSha256(password, salt + be), t = u;
        for (int i = 1; i < iterations; ++i) {
            u = hmacSha256(password, u);
            for (size_t k = 0; k < t.size(); ++k) t[k] ^= u[k];
        }
        out += t;
    }
    out.resize(length);
    return out;
}

// ---------- random bytes from the operating system ----------
// Windows: RtlGenRandom (exported by advapi32 as SystemFunction036), loaded at run time so no extra
// linker flag is needed. Other systems: /dev/urandom. std::random_device is NOT used: on MinGW 6.3
// it returns the same "random" numbers every run.
inline std::string secureRandom(size_t n) {
    std::string out(n, '\0');
#ifdef _WIN32
    typedef BOOLEAN (WINAPI* GenRandom)(PVOID, ULONG);
    static GenRandom fn = nullptr;
    if (!fn) {
        HMODULE lib = LoadLibraryA("advapi32.dll");
        if (lib) fn = (GenRandom)(void*)GetProcAddress(lib, "SystemFunction036");
    }
    if (!fn || !fn(&out[0], (ULONG)n)) throw std::runtime_error("secure random generator unavailable");
#else
    std::ifstream f("/dev/urandom", std::ios::binary);
    if (!f.read(&out[0], (std::streamsize)n)) throw std::runtime_error("secure random generator unavailable");
#endif
    return out;
}

// ---------- encodings ----------
inline std::string toHex(const std::string& bytes) {
    static const char* d = "0123456789abcdef";
    std::string o;
    for (unsigned char c : bytes) { o += d[c >> 4]; o += d[c & 15]; }
    return o;
}
inline bool fromHex(const std::string& hex, std::string& out) {
    if (hex.size() % 2) return false;
    out.clear();
    for (size_t i = 0; i < hex.size(); i += 2) {
        int v = 0;
        for (int k = 0; k < 2; ++k) {
            char c = hex[i + k];
            int x = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (x < 0) return false;
            v = v * 16 + x;
        }
        out += (char)v;
    }
    return true;
}
inline bool isHex(const std::string& s, size_t len) {
    if (s.size() != len) return false;
    for (char c : s) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}
inline std::string base64Encode(const std::string& in) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    unsigned val = 0;
    int bits = -6;
    for (unsigned char c : in) {
        val = (val << 8) | c;
        bits += 8;
        while (bits >= 0) { out += t[(val >> bits) & 0x3F]; bits -= 6; }
    }
    if (bits > -6) out += t[((val << 8) >> (bits + 8)) & 0x3F];
    while (out.size() % 4) out += '=';
    return out;
}
// Strict base64 decoding (standard alphabet, padding optional). Returns false on any bad character.
inline bool base64Decode(const std::string& in, std::string& out) {
    out.clear();
    unsigned val = 0;
    int bits = 0;
    size_t pad = 0;
    for (char c : in) {
        int d;
        if (c >= 'A' && c <= 'Z') d = c - 'A';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 26;
        else if (c >= '0' && c <= '9') d = c - '0' + 52;
        else if (c == '+') d = 62;
        else if (c == '/') d = 63;
        else if (c == '=') { ++pad; continue; }
        else return false;
        if (pad) return false;                               // data after padding
        val = (val << 6) | (unsigned)d;
        bits += 6;
        if (bits >= 8) { bits -= 8; out += (char)((val >> bits) & 0xFF); }
    }
    return pad <= 2;
}

// Compares secrets without stopping at the first difference, so timing reveals nothing.
inline bool constantTimeEquals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i) diff |= (unsigned char)(a[i] ^ b[i]);
    return diff == 0;
}

} // namespace unigo
