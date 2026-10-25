// Offline renderer: input (WAV file or generated signal) + JSON parameters ->
// output WAV, optional per-control-block probe CSV, and golden-file
// regression checks.
//
//   render config.json [-o out.wav] [--probe probe.csv]
//                      [--golden golden.csv (--check | --write)]
//
// The JSON config:
//   { "sampleRate": 48000, "seconds": 4,
//     "input":     {"generate": "snare", "seed": 1, "bpm": 100} | {"file": "in.wav"},
//     "sidechain": {...same as input...}            (optional)
//     "params": { "global": {...}, "groups": [{...}], "bands": [{...}] } }
// Field names match eeq::BandParams / GroupParams / GlobalParams; enums are
// given by name (e.g. "type": "Peak", "tap": "Aux").

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>

#include "Signals.h"
#include "Wav.h"
#include "engine/Engine.h"

using nlohmann::json;
using namespace eeq;

namespace {

template <typename E, size_t N>
E parseEnum(const json& j, const char* key, E def, const std::pair<const char*, E> (&names)[N]) {
    if (!j.contains(key)) return def;
    const std::string s = j.at(key).get<std::string>();
    for (const auto& [name, value] : names)
        if (s == name) return value;
    throw std::runtime_error(std::string("bad value for ") + key + ": " + s);
}

template <typename T>
void opt(const json& j, const char* key, T& out) {
    if (j.contains(key)) out = j.at(key).get<T>();
}

const std::pair<const char*, BandType> kBandTypes[] = {
    {"Off", BandType::Off}, {"Peak", BandType::Peak}, {"LowShelf", BandType::LowShelf},
    {"HighShelf", BandType::HighShelf}, {"LP12", BandType::LP12}, {"HP12", BandType::HP12},
    {"Notch", BandType::Notch}, {"BP", BandType::BP}, {"LP24_LR", BandType::LP24_LR},
    {"HP24_LR", BandType::HP24_LR}, {"Tilt", BandType::Tilt}, {"FletcherMunson", BandType::FletcherMunson},
    {"Vowel", BandType::Vowel}, {"KWeight", BandType::KWeight}};
const std::pair<const char*, DynTarget> kDynTargets[] = {
    {"Gain", DynTarget::Gain}, {"Freq", DynTarget::Freq}, {"Morph", DynTarget::Morph}};
const std::pair<const char*, Tap> kTaps[] = {{"Main", Tap::Main}, {"Aux", Tap::Aux}};
const std::pair<const char*, Chan> kChans[] = {
    {"Stereo", Chan::Stereo}, {"Left", Chan::Left}, {"Right", Chan::Right}, {"Mid", Chan::Mid}, {"Side", Chan::Side}};
const std::pair<const char*, VowelWeighting> kWeightings[] = {
    {"Natural", VowelWeighting::Natural}, {"Flat", VowelWeighting::Flat}};
const std::pair<const char*, DetectorInput> kInputs[] = {
    {"InternalDry", DetectorInput::InternalDry}, {"InternalBandSum", DetectorInput::InternalBandSum},
    {"ExternalSC", DetectorInput::ExternalSC}};
const std::pair<const char*, ScFilter> kScFilters[] = {
    {"Off", ScFilter::Off}, {"BP", ScFilter::BP}, {"LP", ScFilter::LP}, {"HP", ScFilter::HP},
    {"Tilt", ScFilter::Tilt}, {"KWeight", ScFilter::KWeight}};
const std::pair<const char*, TriggerMode> kModes[] = {{"Above", TriggerMode::Above}, {"Below", TriggerMode::Below}};
const std::pair<const char*, OutputMode> kOutputModes[] = {
    {"Stereo", OutputMode::Stereo}, {"SplitLR", OutputMode::SplitLR}, {"SplitMS", OutputMode::SplitMS}};

EngineParams parseParams(const json& j) {
    EngineParams p;
    if (j.contains("global")) {
        const json& g = j["global"];
        GlobalParams& gp = p.global;
        gp.outputMode = parseEnum(g, "outputMode", gp.outputMode, kOutputModes);
        opt(g, "globalScale", gp.globalScale);
        opt(g, "delta", gp.delta);
        opt(g, "bypass", gp.bypass);
        opt(g, "outputGainDb", gp.outputGainDb);
        opt(g, "lookaheadMs", gp.lookaheadMs);
    }
    if (j.contains("groups")) {
        for (size_t i = 0; i < j["groups"].size() && i < kNumGroups; ++i) {
            const json& g = j["groups"][i];
            GroupParams& gp = p.groups[i];
            gp.input = parseEnum(g, "input", gp.input, kInputs);
            gp.scFilter = parseEnum(g, "scFilter", gp.scFilter, kScFilters);
            gp.mode = parseEnum(g, "mode", gp.mode, kModes);
            opt(g, "scHz", gp.scHz);
            opt(g, "scQ", gp.scQ);
            opt(g, "scListen", gp.scListen);
            opt(g, "rmsWindowMs", gp.rmsWindowMs);
            opt(g, "alpha", gp.alpha);
            opt(g, "link", gp.link);
            opt(g, "thresholdDb", gp.thresholdDb);
            opt(g, "riseDb", gp.riseDb);
            opt(g, "kneeDb", gp.kneeDb);
            opt(g, "attackMs", gp.attackMs);
            opt(g, "holdMs", gp.holdMs);
            opt(g, "releaseMs", gp.releaseMs);
        }
    }
    if (j.contains("bands")) {
        for (size_t i = 0; i < j["bands"].size() && i < kNumBands; ++i) {
            const json& b = j["bands"][i];
            BandParams& bp = p.bands[i];
            bp.type = parseEnum(b, "type", bp.type, kBandTypes);
            bp.dynTarget = parseEnum(b, "dynTarget", bp.dynTarget, kDynTargets);
            bp.tap = parseEnum(b, "tap", bp.tap, kTaps);
            bp.chan = parseEnum(b, "chan", bp.chan, kChans);
            bp.option = static_cast<int>(parseEnum(b, "weighting", static_cast<VowelWeighting>(bp.option), kWeightings));
            opt(b, "x", bp.x);
            opt(b, "y", bp.y);
            opt(b, "z", bp.z);
            opt(b, "dynGroup", bp.dynGroup);
            opt(b, "dynRange", bp.dynRange);
            opt(b, "bypass", bp.bypass);
            opt(b, "solo", bp.solo);
        }
    }
    return p;
}

wav::Audio loadSource(const json& j, double fs, double seconds, const std::string& baseDir) {
    if (j.contains("file")) {
        std::string path = j["file"].get<std::string>();
        if (!path.empty() && path[0] != '/') path = baseDir + path;
        wav::Audio a = wav::read(path);
        if (a.sampleRate != fs) throw std::runtime_error("input sample rate does not match config");
        return a;
    }
    return signals::generate(j.at("generate").get<std::string>(), fs, seconds, j.value("seed", 1u),
                             j.value("bpm", 100.0));
}

// 10 ms RMS frames in dB, per channel.
std::vector<std::array<double, 2>> rmsFrames(const wav::Audio& a) {
    const size_t frame = static_cast<size_t>(a.sampleRate / 100);
    std::vector<std::array<double, 2>> out;
    for (size_t s = 0; s + frame <= a.l.size(); s += frame) {
        double el = 0, er = 0;
        for (size_t i = s; i < s + frame; ++i) el += double(a.l[i]) * a.l[i], er += double(a.r[i]) * a.r[i];
        auto db = [&](double e) { return std::max(-100.0, 10.0 * std::log10(e / frame + 1e-30)); };
        out.push_back({db(el), db(er)});
    }
    return out;
}

int checkGolden(const std::string& path, const std::vector<std::array<double, 2>>& frames) {
    std::ifstream f(path);
    if (!f) {
        std::cerr << "missing golden file " << path << "\n";
        return 1;
    }
    std::string line;
    size_t i = 0, bad = 0;
    double worst = 0.0;
    while (std::getline(f, line)) {
        double l, r;
        if (std::sscanf(line.c_str(), "%lf,%lf", &l, &r) != 2) continue;
        if (i >= frames.size()) return std::cerr << "golden length mismatch\n", 1;
        // Tolerance: 0.1 dB, relaxed for very quiet frames.
        for (int c = 0; c < 2; ++c) {
            const double ref = c ? r : l, err = std::abs(frames[i][c] - ref);
            const double tol = ref < -80.0 ? 3.0 : 0.1;
            worst = std::max(worst, err);
            if (err > tol) ++bad;
        }
        ++i;
    }
    if (i != frames.size()) return std::cerr << "golden length mismatch\n", 1;
    std::cout << "golden: " << i << " frames, worst diff " << worst << " dB, " << bad << " out of tolerance\n";
    return bad ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) try {
    std::string config, outPath, probePath, goldenPath;
    bool check = false, write = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("missing value for " + a);
            return argv[++i];
        };
        if (a == "-o") outPath = next();
        else if (a == "--probe") probePath = next();
        else if (a == "--golden") goldenPath = next();
        else if (a == "--check") check = true;
        else if (a == "--write") write = true;
        else if (config.empty()) config = a;
        else throw std::runtime_error("unexpected argument " + a);
    }
    if (config.empty()) {
        std::cerr << "usage: render config.json [-o out.wav] [--probe probe.csv] [--golden g.csv --check|--write]\n";
        return 2;
    }
    std::ifstream cf(config);
    if (!cf) throw std::runtime_error("cannot read " + config);
    const json j = json::parse(cf);
    const std::string baseDir = config.find('/') == std::string::npos ? "" : config.substr(0, config.rfind('/') + 1);
    const double fs = j.value("sampleRate", 48000.0);
    const double seconds = j.value("seconds", 4.0);

    const wav::Audio in = loadSource(j.at("input"), fs, seconds, baseDir);
    wav::Audio sc;
    const bool hasSc = j.contains("sidechain");
    if (hasSc) sc = loadSource(j["sidechain"], fs, seconds, baseDir);

    Engine engine;
    engine.setParams(parseParams(j.value("params", json::object())));
    engine.prepare(fs);

    wav::Audio out;
    out.sampleRate = fs;
    out.l.resize(in.l.size()), out.r.resize(in.l.size());
    std::ofstream probe;
    if (!probePath.empty()) {
        probe.open(probePath);
        probe << "time";
        for (int g = 0; g < kNumGroups; ++g) probe << ",D" << g << ",trigger" << g << ",teff" << g;
        probe << "\n";
    }
    for (size_t pos = 0; pos < in.l.size(); pos += kControlBlock) {
        const int n = static_cast<int>(std::min<size_t>(kControlBlock, in.l.size() - pos));
        const float* ip[2] = {in.l.data() + pos, in.r.data() + pos};
        float* op[2] = {out.l.data() + pos, out.r.data() + pos};
        const float* sp[2] = {nullptr, nullptr};
        if (hasSc && pos + n <= sc.l.size()) sp[0] = sc.l.data() + pos, sp[1] = sc.r.data() + pos;
        engine.process(ip, sp[0] ? sp : nullptr, op, n);
        if (probe.is_open()) {
            probe << pos / fs;
            for (int g = 0; g < kNumGroups; ++g)
                probe << "," << engine.group(g).control(0) << "," << engine.group(g).trigger(0) << ","
                      << engine.group(g).effectiveThreshold();
            probe << "\n";
        }
    }

    if (!outPath.empty()) wav::write(outPath, out);
    if (!goldenPath.empty()) {
        const auto frames = rmsFrames(out);
        if (write) {
            std::ofstream g(goldenPath);
            g.precision(4);
            g << std::fixed << "# 10 ms RMS frames (dB): left,right\n";
            for (const auto& fr : frames) g << fr[0] << "," << fr[1] << "\n";
            std::cout << "wrote " << goldenPath << "\n";
        } else if (check) {
            return checkGolden(goldenPath, frames);
        }
    }
    return 0;
} catch (const std::exception& e) {
    std::cerr << "render: " << e.what() << "\n";
    return 1;
}
