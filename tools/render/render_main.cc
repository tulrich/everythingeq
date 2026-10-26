// Offline renderer: input (WAV file or generated signal) + JSON parameters ->
// output WAV, optional per-control-block probe CSV, and golden-file
// regression checks.
//
//   render config.json [-o out.wav] [--probe probe.csv]
//                      [--golden golden.csv (--check | --write)]
//
// The JSON config:
//   { "sampleRate": 48000, "seconds": 4,
//     "preset": "path/to/preset.json",  (optional, relative to the config)
//     "input": {"generate": "snare", "seed": 1, "bpm": 100}
//              | {"file": "in.wav"},
//     "sidechain": {...same as input...},  (optional)
//     "params": {"global": {...}, "groups": [{...}], "bands": [{...}]}}
// "params" (optional) overrides the preset; its schema is described in
// model/engine_params_json.h.

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>

#include "dsp/engine/engine.h"
#include "model/engine_params_json.h"
#include "model/presets.h"
#include "tools/render/signals.h"
#include "tools/render/wav.h"

namespace eeq {

using nlohmann::json;

namespace {

wav::Audio LoadSource(const json& j, double fs, double seconds,
                      const std::string& base_dir) {
  if (j.contains("file")) {
    std::string path = j["file"].get<std::string>();
    if (!path.empty() && path[0] != '/') path = base_dir + path;
    wav::Audio a = wav::Read(path);
    if (a.sample_rate != fs)
      throw std::runtime_error("input sample rate does not match config");
    return a;
  }
  return signals::Generate(j.at("generate").get<std::string>(), fs, seconds,
                           j.value("seed", 1u), j.value("bpm", 100.0));
}

// 10 ms RMS frames in dB, per channel.
std::vector<std::array<double, 2>> RmsFrames(const wav::Audio& a) {
  const size_t frame = static_cast<size_t>(a.sample_rate / 100);
  std::vector<std::array<double, 2>> out;
  for (size_t s = 0; s + frame <= a.l.size(); s += frame) {
    double el = 0, er = 0;
    for (size_t i = s; i < s + frame; ++i)
      el += double(a.l[i]) * a.l[i], er += double(a.r[i]) * a.r[i];
    auto db = [&](double e) {
      return std::max(-100.0, 10.0 * std::log10(e / frame + 1e-30));
    };
    out.push_back({db(el), db(er)});
  }
  return out;
}

int CheckGolden(const std::string& path,
                const std::vector<std::array<double, 2>>& frames) {
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
  std::cout << "golden: " << i << " frames, worst diff " << worst << " dB, "
            << bad << " out of tolerance\n";
  return bad ? 1 : 0;
}

}  // namespace

int RenderMain(int argc, char** argv) {
  std::string config, out_path, probe_path, golden_path;
  bool check = false, write = false;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) throw std::runtime_error("missing value for " + a);
      return argv[++i];
    };
    if (a == "-o")
      out_path = next();
    else if (a == "--probe")
      probe_path = next();
    else if (a == "--golden")
      golden_path = next();
    else if (a == "--check")
      check = true;
    else if (a == "--write")
      write = true;
    else if (config.empty())
      config = a;
    else
      throw std::runtime_error("unexpected argument " + a);
  }
  if (config.empty()) {
    std::cerr << "usage: render config.json [-o out.wav] [--probe probe.csv] "
                 "[--golden g.csv --check|--write]\n";
    return 2;
  }
  std::ifstream cf(config);
  if (!cf) throw std::runtime_error("cannot read " + config);
  const json j = json::parse(cf);
  const std::string base_dir = config.find('/') == std::string::npos
                                   ? ""
                                   : config.substr(0, config.rfind('/') + 1);
  const double fs = j.value("sampleRate", 48000.0);
  const double seconds = j.value("seconds", 4.0);

  const wav::Audio in = LoadSource(j.at("input"), fs, seconds, base_dir);
  wav::Audio sc;
  const bool has_sc = j.contains("sidechain");
  if (has_sc) sc = LoadSource(j["sidechain"], fs, seconds, base_dir);

  Engine engine;
  // Parameters: an optional preset file, then optional overrides.
  EngineParams params;
  if (j.contains("preset")) {
    const std::string path = base_dir + j["preset"].get<std::string>();
    std::string error;
    const std::optional<Preset> preset = LoadPresetFile(path, &error);
    if (!preset) throw std::runtime_error(path + ": " + error);
    params = preset->params;
  }
  ApplyEngineParamsJson(j.value("params", json::object()), params);
  engine.SetParams(params);
  engine.Prepare(fs);

  wav::Audio out;
  out.sample_rate = fs;
  out.l.resize(in.l.size()), out.r.resize(in.l.size());
  std::ofstream probe;
  if (!probe_path.empty()) {
    probe.open(probe_path);
    probe << "time";
    for (int g = 0; g < kNumGroups; ++g)
      probe << ",D" << g << ",trigger" << g << ",teff" << g;
    probe << "\n";
  }
  for (size_t pos = 0; pos < in.l.size(); pos += kControlBlock) {
    const int n =
        static_cast<int>(std::min<size_t>(kControlBlock, in.l.size() - pos));
    const float* ip[2] = {in.l.data() + pos, in.r.data() + pos};
    float* op[2] = {out.l.data() + pos, out.r.data() + pos};
    const float* sp[2] = {nullptr, nullptr};
    if (has_sc && pos + n <= sc.l.size())
      sp[0] = sc.l.data() + pos, sp[1] = sc.r.data() + pos;
    engine.Process(ip, sp[0] ? sp : nullptr, op, n);
    if (probe.is_open()) {
      probe << pos / fs;
      for (int g = 0; g < kNumGroups; ++g)
        probe << "," << engine.group(g).control(0) << ","
              << engine.group(g).trigger(0) << ","
              << engine.group(g).effective_threshold();
      probe << "\n";
    }
  }

  if (!out_path.empty()) wav::Write(out_path, out);
  if (!golden_path.empty()) {
    const auto frames = RmsFrames(out);
    if (write) {
      std::ofstream g(golden_path);
      g.precision(4);
      g << std::fixed << "# 10 ms RMS frames (dB): left,right\n";
      for (const auto& fr : frames) g << fr[0] << "," << fr[1] << "\n";
      std::cout << "wrote " << golden_path << "\n";
    } else if (check) {
      return CheckGolden(golden_path, frames);
    }
  }
  return 0;
}

}  // namespace eeq

int main(int argc, char** argv) {
  try {
    return eeq::RenderMain(argc, argv);
  } catch (const std::exception& e) {
    std::cerr << "render: " << e.what() << "\n";
    return 1;
  }
}
