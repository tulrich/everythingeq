// Phase 6: the audio-thread path is real-time safe, and Lookahead changes
// are gated for hosts that re-read latency only on transport start.

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <new>
#include <random>
#include <vector>

#include "dsp/engine/engine.h"
#include "model/lookahead_gate.h"
#include "model/param_table.h"

// Counting replacements for the global allocation functions.  Only
// allocations made on a thread while `g_counting` is set are counted.
namespace {
thread_local bool g_counting = false;
thread_local int g_allocations = 0;

void* CountedAlloc(std::size_t size) {
  if (g_counting) ++g_allocations;
  if (void* p = std::malloc(size ? size : 1)) return p;
  throw std::bad_alloc();
}
}  // namespace

void* operator new(std::size_t size) { return CountedAlloc(size); }
void* operator new[](std::size_t size) { return CountedAlloc(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace eeq::test {
namespace {

class CountAllocations {
 public:
  CountAllocations() {
    g_allocations = 0;
    g_counting = true;
  }
  ~CountAllocations() { g_counting = false; }
  int count() const { return g_allocations; }
};

}  // namespace

TEST_CASE("Allocation counter detects allocations", "[realtime]") {
  CountAllocations counter;
  std::vector<int> v(100);
  REQUIRE(counter.count() > 0);
}

TEST_CASE("Audio-thread path does not allocate", "[realtime]") {
  // What the plugin does per block: rebuild EngineParams from host values,
  // hand them to the engine, and process.  Every parameter changes on every
  // block (worst case: type changes, solos, SC listen, lookahead...).
  constexpr int kBlock = 64;
  std::vector<float> in_l(kBlock), in_r(kBlock), out_l(kBlock), out_r(kBlock);
  for (int i = 0; i < kBlock; ++i) in_l[i] = in_r[i] = 0.1f * (i % 7);
  const float* in[2] = {in_l.data(), in_r.data()};
  float* out[2] = {out_l.data(), out_r.data()};

  std::mt19937 rng(9);
  std::uniform_real_distribution<double> unit(0.0, 1.0);
  ParamValues values = DefaultParamValues();  // Initializes the table.
  Engine engine;
  engine.Prepare(48000.0);

  CountAllocations counter;
  for (int block = 0; block < 500; ++block) {
    for (int id = 0; id < kNumParams; ++id) {
      const ParamSpec& s = ParamTable()[id];
      values[id] = SanitizeParamValue(id, s.min + unit(rng) * (s.max - s.min));
    }
    engine.SetParams(BuildEngineParams(values, block % 2 == 0));
    engine.Process(in, block % 3 ? in : nullptr, out, kBlock);
  }
  REQUIRE(counter.count() == 0);
}

TEST_CASE("Lookahead gate holds changes until the transport starts",
          "[realtime]") {
  LookaheadGate gate(/*host_defers_latency=*/true);
  gate.Reset(0.0);
  // Stopped: changes apply at once.
  REQUIRE(gate.Update(2.0, /*running=*/false, /*started=*/false) == 2.0);
  // Playing: hold the applied value...
  REQUIRE(gate.Update(5.0, true, true) == 5.0);
  REQUIRE(gate.Update(8.0, true, false) == 5.0);
  REQUIRE(gate.Update(9.0, true, false) == 5.0);
  // ...until the next transport start, or a reset.
  REQUIRE(gate.Update(9.0, true, true) == 9.0);
  REQUIRE(gate.Update(1.0, true, false) == 9.0);
  gate.Reset(1.0);
  REQUIRE(gate.applied_ms() == 1.0);

  LookaheadGate immediate(/*host_defers_latency=*/false);
  REQUIRE(immediate.Update(4.0, true, false) == 4.0);
}

}  // namespace eeq::test
