#ifndef EVERYTHINGEQ_DSP_COMMON_DENORMALS_H_
#define EVERYTHINGEQ_DSP_COMMON_DENORMALS_H_

#include <cstdint>

#if defined(__SSE__) || defined(_M_X64) || \
    (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
#include <xmmintrin.h>
#define EEQ_DENORMALS_SSE 1
#endif

namespace eeq {

// RAII scope that enables flush-to-zero / denormals-are-zero on the current
// thread and restores the previous mode on exit. Use on the audio thread.
class ScopedNoDenormals {
 public:
  ScopedNoDenormals() {
#if defined(EEQ_DENORMALS_SSE)
    saved_ = _mm_getcsr();
    _mm_setcsr(static_cast<unsigned>(saved_) | 0x8040u);  // FTZ | DAZ
#elif defined(__aarch64__)
    uint64_t fpcr;
    asm volatile("mrs %0, fpcr" : "=r"(fpcr));
    saved_ = fpcr;
    asm volatile("msr fpcr, %0" : : "r"(fpcr | (uint64_t{1} << 24)));  // FZ
#endif
  }
  ~ScopedNoDenormals() {
#if defined(EEQ_DENORMALS_SSE)
    _mm_setcsr(static_cast<unsigned>(saved_));
#elif defined(__aarch64__)
    asm volatile("msr fpcr, %0" : : "r"(saved_));
#endif
  }
  ScopedNoDenormals(const ScopedNoDenormals&) = delete;
  ScopedNoDenormals& operator=(const ScopedNoDenormals&) = delete;

 private:
  [[maybe_unused]] uint64_t saved_ = 0;
};

}  // namespace eeq

#endif  // EVERYTHINGEQ_DSP_COMMON_DENORMALS_H_
