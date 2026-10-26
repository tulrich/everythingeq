#ifndef EVERYTHINGEQ_DSP_ENGINE_DELAY_LINE_H_
#define EVERYTHINGEQ_DSP_ENGINE_DELAY_LINE_H_

#include <algorithm>
#include <vector>

namespace eeq {

// Fixed-capacity integer delay line.  Allocates only in prepare().
class DelayLine {
 public:
  void Prepare(int max_delay) {
    buf_.assign(static_cast<size_t>(max_delay) + 1, 0.0);
    pos_ = 0;
  }
  void Reset() {
    std::fill(buf_.begin(), buf_.end(), 0.0);
    pos_ = 0;
  }
  // Push x and return the sample from `delay` samples ago.
  double Process(double x, int delay) {
    buf_[pos_] = x;
    int read = pos_ - delay;
    if (read < 0) read += static_cast<int>(buf_.size());
    const double y = buf_[read];
    if (++pos_ == static_cast<int>(buf_.size())) pos_ = 0;
    return y;
  }

 private:
  std::vector<double> buf_;
  int pos_ = 0;
};

}  // namespace eeq

#endif  // EVERYTHINGEQ_DSP_ENGINE_DELAY_LINE_H_
