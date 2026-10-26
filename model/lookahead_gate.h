#ifndef EVERYTHINGEQ_MODEL_LOOKAHEAD_GATE_H_
#define EVERYTHINGEQ_MODEL_LOOKAHEAD_GATE_H_

namespace eeq {

// Decides when a Lookahead change reaches the audio path (ABOUT.md section
// 3.B).  The plugin reports the requested latency to the host immediately,
// but some hosts (Logic and GarageBand with AU) only re-read the latency when
// the transport starts.  Changing the audio delay before then would misalign
// the track until playback restarts, so for those hosts the gate holds the
// previously applied value while the transport runs and applies the request
// on the next transport start (or reset).  Other hosts restart the plugin or
// re-query latency when it changes, so the request applies at once.
//
// Audio thread only.
class LookaheadGate {
 public:
  explicit LookaheadGate(bool host_defers_latency = false)
      : host_defers_latency_(host_defers_latency) {}

  void set_host_defers_latency(bool defers) { host_defers_latency_ = defers; }

  // Call on reset/activation: the host re-reads latency then.
  void Reset(double requested_ms) { applied_ms_ = requested_ms; }

  // Call once per block.  Returns the lookahead the engine should use.
  double Update(double requested_ms, bool transport_running,
                bool transport_started) {
    if (!host_defers_latency_ || !transport_running || transport_started) {
      applied_ms_ = requested_ms;
    }
    return applied_ms_;
  }

  double applied_ms() const { return applied_ms_; }

 private:
  bool host_defers_latency_;
  double applied_ms_ = 0.0;
};

}  // namespace eeq

#endif  // EVERYTHINGEQ_MODEL_LOOKAHEAD_GATE_H_
