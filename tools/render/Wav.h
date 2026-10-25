#pragma once

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace wav {

struct Audio {
    double sampleRate = 48000.0;
    std::vector<float> l, r;
};

inline void write(const std::string& path, const Audio& a) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot write " + path);
    const uint32_t frames = static_cast<uint32_t>(a.l.size());
    const uint32_t dataBytes = frames * 2 * 4;
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    f.write("RIFF", 4), u32(36 + dataBytes), f.write("WAVE", 4);
    f.write("fmt ", 4), u32(16), u16(3), u16(2);
    u32(static_cast<uint32_t>(a.sampleRate)), u32(static_cast<uint32_t>(a.sampleRate) * 8), u16(8), u16(32);
    f.write("data", 4), u32(dataBytes);
    for (uint32_t i = 0; i < frames; ++i) {
        f.write(reinterpret_cast<const char*>(&a.l[i]), 4);
        f.write(reinterpret_cast<const char*>(&a.r[i]), 4);
    }
}

// Reads PCM 16/24/32-bit or float32 WAV, mono or stereo (mono is duplicated).
inline Audio read(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot read " + path);
    std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto u32at = [&](size_t p) { uint32_t v; std::memcpy(&v, &bytes[p], 4); return v; };
    auto u16at = [&](size_t p) { uint16_t v; std::memcpy(&v, &bytes[p], 2); return v; };
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) || std::memcmp(&bytes[8], "WAVE", 4))
        throw std::runtime_error(path + ": not a WAV file");
    uint16_t format = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    size_t pos = 12;
    Audio a;
    while (pos + 8 <= bytes.size()) {
        const uint32_t size = u32at(pos + 4);
        if (!std::memcmp(&bytes[pos], "fmt ", 4)) {
            format = u16at(pos + 8), channels = u16at(pos + 10), rate = u32at(pos + 12), bits = u16at(pos + 22);
            if (format == 0xFFFE) format = u16at(pos + 32);  // WAVE_FORMAT_EXTENSIBLE subformat
        } else if (!std::memcmp(&bytes[pos], "data", 4)) {
            if (channels < 1 || channels > 2) throw std::runtime_error(path + ": need mono or stereo");
            const size_t bps = bits / 8, frames = size / (bps * channels);
            a.sampleRate = rate;
            a.l.resize(frames), a.r.resize(frames);
            for (size_t i = 0; i < frames; ++i) {
                for (size_t c = 0; c < channels; ++c) {
                    const char* s = &bytes[pos + 8 + (i * channels + c) * bps];
                    float v = 0.0f;
                    if (format == 3 && bits == 32) std::memcpy(&v, s, 4);
                    else if (format == 1 && bits == 16) { int16_t x; std::memcpy(&x, s, 2); v = x / 32768.0f; }
                    else if (format == 1 && bits == 24) {
                        int32_t x = (uint8_t(s[0]) | uint8_t(s[1]) << 8 | int8_t(s[2]) * 65536);
                        v = x / 8388608.0f;
                    } else if (format == 1 && bits == 32) { int32_t x; std::memcpy(&x, s, 4); v = x / 2147483648.0f; }
                    else throw std::runtime_error(path + ": unsupported sample format");
                    (c == 0 ? a.l : a.r)[i] = v;
                }
                if (channels == 1) a.r[i] = a.l[i];
            }
            return a;
        }
        pos += 8 + size + (size & 1);
    }
    throw std::runtime_error(path + ": no data chunk");
}

}  // namespace wav
