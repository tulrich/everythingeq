# Everything EQ — Implementation Plan

This plan implements the spec in [ABOUT.md](ABOUT.md). Section and decision references (§3.B, D4, …) point there.

## Guiding approach
* **DSP first, offline, test-driven.** `dsp/` is a framework-free C++20 static library with no allocation, locks or I/O on the audio path. Every DSP feature lands with unit tests and an offline render before it is wired to a UI.
* **Thin wrapper.** `plugin/` (iPlug2) only maps parameters, I/O buses, state and UI onto the `dsp/` engine.
* **One source of truth for curves.** The UI's EQ curve is computed from the same coefficient code as the audio (analytic magnitude response), never re-derived separately.
* **Thread model.** Audio thread: process only. UI thread: drawing, FFT analysis, response curves. Communication: atomics for parameters, lock-free SPSC FIFOs for meter, analyzer and trigger data.

## Repository layout
```
dsp/                 framework-free engine (C++20)
  svf/               ZDF SVF cell, coefficient design, response evaluation
  bands/             band types, polymorphic band slot, tap logic
  dynamics/          sidechain conditioning, detector, gain computer, ballistics
  engine/            chain, groups, routing/output modes, parameter snapshot
tests/               unit + property tests (Catch2), golden renders
tools/
  render/            CLI: wav in + param JSON → wav out (+ CSV of D_m, trigger)
  plot/              scripts to plot magnitude responses / envelopes (Python, dev only)
plugin/              iPlug2 project: params, I/O config, state, presets
  ui/                IGraphics controls and layout
presets/             factory presets (JSON source → embedded)
.github/workflows/   CI
```

## Phase 0 — Foundations & spikes (de-risk first)
1. **Toolchain:** CMake (C++20) for `dsp/`, `tests/` and `tools/`; Catch2 via FetchContent. Add a pinned iPlug2 git submodule; build the plugin with iPlug2's project setup (evaluate its CMake support vs. its Xcode/VS project templates and pick one).
2. **Spike A (formats):** iPlug2 "hello gain" plugin with a stereo in, stereo sidechain in, stereo out layout, built as AU, VST3 and CLAP on macOS and as VST3 and CLAP on Windows. Verify that the sidechain works in Logic, Reaper, Bitwig, Ableton Live and Cubase. Confirm the state of iPlug2's CLAP support; if it falls short, fall back to clap-wrapper over the VST3 build.
3. **Spike B (UI):** IGraphics backend choice (NanoVG vs Skia), HiDPI and scaling, plus a 60 fps analyzer test at 960×560.
4. **CI:** GitHub Actions matrix (macos-latest universal, windows-latest), running the build, unit tests, `pluginval`, `auval` (macOS) and `clap-validator`.

**Exit:** gain plugin loads and passes the validators in every target host, and CI is green.

## Phase 1 — Filter core (§3.A)
1. `SvfCell`: Simper/Zavalishin TPT SVF with outputs (LP, BP, HP) and mix weights for each shape (Peak, Low/High Shelf, LP12, HP12, Notch, BP). Use the standard Cytomic shelf/bell gain mappings ($A = 10^{\text{dB}/40}$).
2. Coefficient designer: frequency/Q/gain → (g, k, mix weights), with algebraic Q-prewarp near Nyquist.
3. `analyticResponse(f)` for each cell and cascade, evaluated in the z-domain from the same coefficients.
4. Control-rate scheme: 16-sample sub-blocks, dirty epsilon, linear interpolation of g/k/mix across the sub-block.
5. Denormal protection (FTZ/DAZ scope guard plus a tiny DC offset where needed).

**Tests:**
* Impulse-response FFT matches `analyticResponse` within 0.01 dB (20 Hz–0.45 fs) at 44.1, 48, 96 and 192 kHz.
* Bell/shelf gain at center and plateau within ±0.05 dB.
* Click test: sweep cutoff 20 Hz→20 kHz in 50 ms on a sine; energy above the expected spectrum stays below a threshold. Compare against a reference biquad to prove the benefit.
* No NaN/Inf/denormals after 10 s of silence following full-scale noise.

## Phase 2 — Band types (§3.A, §3.C)
1. LP24_LR / HP24_LR (2× Butterworth SVF). Test: LP+HP magnitude flat within 0.01 dB, phase equals the 2nd-order all-pass.
2. Tilt (inverse shelves around f0). Test: target dB/oct slope, 0 dB at f0.
3. K-Weighting. Test: matches BS.1770 reference response within 0.1 dB, 20 Hz–20 kHz, at all supported sample rates.
4. Fletcher-Munson: offline fit (tools/) of the 3-cell cascade to ISO 226:2003 contour differences for several phon deltas. Store fitted parameters as a small table interpolated by Phon Delta, with 0 dB at 1 kHz enforced. Test: fit error report; 1 kHz anchor within 0.1 dB.
5. Vowel: morph table, log-frequency interpolation, Tract Scale, Natural/Flat weighting, fixed Qs. Test: formant centers land within 1% for each anchor and intermediate morph value.
6. `BandSlot`: polymorphic wrapper (a variant/switch, no virtual calls per sample) with up to 4 cells. Changing type resets state smoothly by crossfading 16–64 samples between old and new configuration to avoid clicks.

## Phase 3 — Chain, routing & output modes (§3.D)
1. Series chain of 10 slots with per-band Bypass and Chan (Stereo/L/R/Mid/Side via M/S encode/decode around the band).
2. Tap logic per band category (delta / split / LR crossover), with Aux accumulation.
3. Output modes: Stereo, Split L/R, Split M/S (mono-sum input).
4. Delta listen, band Solo (detector filter), global Bypass (crossfaded), Output Gain.

**Tests (property-based, random parameter sets):**
* Recombination identities from §3.D hold within 1e-6 relative error.
* Split M/S: L+R = Main and L−R = Aux exactly.
* Bypassed band equals the identity bit-exactly.
* Stereo mode with taps equals the same configuration with no taps.

## Phase 4 — Dynamics engine (§3.B)
1. Sidechain conditioning: input selector, group SC filter, $x^2$, Link blend.
2. Layer 1: sigA (τ = 1 ms) and sigB (RMS Window) one-pole smoothers on power, then dB with a −70 dB floor.
3. α trigger and $T_{\text{eff}} = (1-\alpha)\,\text{Threshold} + \alpha\,\text{Rise}$; knee ramp; Above/Below mode.
4. Ballistics: attack / hold / release on the normalized scalar → $D_m$.
5. Per-band modulation for Gain (dB), Freq (oct) and Morph targets, with Global Scale and clamping. Modulation is evaluated at control rate and flows through the same interpolation as static changes.
6. Detector filters per band type, and the "Internal Band-Sum" path computed *before* dynamic modulation (feed-forward guarantee).

**Tests:**
* α=0 on a stepped sine: trigger tracks level within 0.1 dB after settling.
* α=1 on a steady sine at −40 and −6 dBFS: trigger ≈ 0 dB at both. Transient bursts at different levels give the same peak trigger within 1 dB.
* Ballistics: measured attack/release times within 5% of setting; hold behaves as specified.
* Feed-forward: a boosting band subscribed to Internal Band-Sum cannot latch (bounded output over a 60 s noise render).
* Resonant sweep stress (touch-wah): Peak `+12 dB`, `Q = 6`, with Freq driven 20 Hz→0.45 fs by a 0–1 square-wave $D_m$ at attack 0.5 ms. Output stays bounded with no NaN, and the Vowel morph sweep across 0→4 gives continuous formant centers (no jumps at anchor boundaries).
* Use-case renders via `tools/render`: snare split, bass crossover, F-M loudness, vowel carving, dynamic HP, touch-wah (Peak + Vowel morph). Listen-check and save as golden files with tolerance-based regression comparison.

## Phase 5 — Parameters, state & presets (§3.E)
1. Parameter table (single declarative list) generating iPlug2 params with IDs, ranges, skew, units and display formatters; X/Y/Z display text depends on band type.
2. Lock-free parameter snapshot to the engine each block; type and group changes are handled without allocation.
3. State serialization with version field and migration hooks; round-trip tests.
4. Factory presets covering the six use cases (touch-wah ships as two presets: Peak and Vowel) plus basic EQ starting points.

## Phase 6 — Plugin integration
1. I/O config: stereo in, stereo sidechain in, stereo out (mono variants where hosts need them). Zero reported latency.
2. Per-format checks: AU (auval, Logic), VST3 (Reaper, Cubase, Live, Bitwig), CLAP (Bitwig, Reaper).
3. Automation smoothness under host automation of every continuous parameter.

## Phase 7 — UI (§4)
1. Layout skeleton at 960×560 with scaling; fixed slots; 35% opacity for inactive controls.
2. Band ribbon (selection, type, active state) and band editor row (type-specific X/Y/Z labels and units).
3. EQ graph: log-frequency grid, composite curve and per-band fills from `analyticResponse`, draggable nodes (drag = freq/gain, wheel = Q), plus the dynamic range shown as a ghost curve at $D_m = 1$ and a live curve at the current $D_m$.
4. Analyzer: Pre/Post/Pre+Post, FFT on the UI thread fed by a lock-free FIFO, display tilt.
5. Dynamics group panel: source and sidechain, detector and continuum, trigger meter with draggable threshold handle (showing Trigger vs $T_{\text{eff}}$), ballistics, Grp Out readout.
6. Global footer: Output Mode, Scale, Δ, Bypass.

## Phase 8 — Performance, QA & release
1. **CPU budget:** worst case (10 bands × 4 cells, 4 groups, stereo, 48 kHz, 64-sample buffers) under 2% of one Apple M1 core. Profile and optimize (SIMD across stereo channels) only if the budget is missed.
2. Edge cases: buffer size 1 and odd sizes, sample-rate changes, silence and DC, extreme parameters, rapid type switching.
3. Host test matrix on macOS (Logic, Reaper, Live, Bitwig, Cubase) and Windows (Reaper, Live, Bitwig, Cubase).
4. Packaging: macOS signed and notarized pkg; Windows installer; user manual derived from ABOUT.md.

## Milestones
| Milestone | Contents | Demo |
| :--- | :--- | :--- |
| M0 | Phase 0 | Gain plugin with sidechain loads in all hosts; CI green |
| M1 | Phases 1–3 | Offline renders of a static 10-band EQ with taps and output modes |
| M2 | Phase 4 | Offline renders of all six use cases |
| M3 | Phases 5–6 | Usable plugin with a generic UI in every host |
| M4 | Phase 7 | Full custom UI |
| M5 | Phase 8 | Release candidate |

## Risks
* **iPlug2 CLAP / sidechain maturity in specific hosts:** de-risked in Spike A; fallback is clap-wrapper.
* **Fletcher-Munson fit quality with only 3 cells:** if the fit error is too high, allow a 4th cell (the slot already supports 4).
* **Narrow dynamic vowel notches can modulate audibly ("zipper" or AM sidebands) at fast attack times:** mitigate with a minimum effective attack per band type, validated by listening tests.
* **Parameter count (~200):** check host automation-list usability; group names by band/group prefix.

## Open items (not blocking M0–M2)
* Exact parameter ranges, skews and defaults table (drafted in Phase 5).
* Whether to expose `sigA` τ (D5) after listening tests.
* Formant Q values and F-M presence-bell center (tuning).
* Post-v1: multi-output buses for VST3/CLAP hosts, an instrument variant for Logic multi-out, formant tracking.
