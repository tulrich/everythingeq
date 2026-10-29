# Everything EQ — Implementation Plan

This plan implements the spec in [ABOUT.md](ABOUT.md). Section and decision references (§3.B, D4, …) point there.

## Status (2026-10-25)
* **Done, Phases 1–4 (milestones M1 and M2, DSP only):** `dsp/` engine, 38 unit tests plus 8 golden use-case renders (`ctest`), `tools/render` and `tools/fmfit`. CI runs the DSP build and tests on macOS (universal) and Windows; the golden renders pass on Windows as well as macOS/arm64.
* **Spec revised (2026-10-25):** FOSS distribution, mono layouts, parameter ranges, listen precedence and UI scope (ABOUT.md D10–D15).
* **Phase 0 (done, milestone M0):** iPlug2 submodule with pinned SDKs (`scripts/setup-deps.sh`) and local CLAP fixes (`scripts/patches/`), plus `plugin/`, a spike that passes audio through `eeq::Engine` as AU/VST3/CLAP/app. All three layouts and the mono/stereo sidechain are declared. `auval -strict`, `pluginval` (strictness 10, AU and VST3) and `clap-validator` pass locally, and CI builds and validates the plugin on macOS and Windows. **Logic checklist (2026-10-25):** all items confirmed: sidechain selection, mono/stereo/mono→stereo layouts, Output Gain automation, meter clip latch/reset, and project save/restore. Spike B (UI performance) moves to the start of Phase 7, measured on the real analyzer and EQ graph rather than a test pattern.
* **Phase 5 (done):** `model/` library (parameter table and slot mappings, host ↔ engine conversion, versioned JSON state, presets, undo history) with 15 new tests; 10 factory presets in `presets/factory/` shared with the use-case renders (which stay byte-identical); the plugin now exposes all 186 parameters, saves/restores state, lists the factory presets in the host and applies the mono → mono rules. All validators pass (clap-validator now with no warnings). Two more iPlug2 fixes are patched locally (see Phase 0 findings).
* **Phase 6 (done):** Lookahead changes are held until the host re-reads latency (Logic/GarageBand: next transport start; `model/lookahead_gate.h`, D18); new tests sweep all 86 automatable continuous parameters at host-automation rate through the host-value path (worst HF residue −83 dB) and verify that the audio-thread path never allocates. Logic checks: layouts, sidechain, automation, state, and lookahead delay compensation.
* **Phase 7 (in progress):** 7a done: `plugin/ui/` editor with the band ribbon, band editor (fields relabel and grey per band type), EQ graph (per-band and composite curves from `model/response_curves`, ghost/live curves for dynamic bands, draggable nodes, wheel = Q, double-click adds a band), In/Out/SC meters, footer and the dynamics group panel (fields only). 7b done: detector scope per group (trigger trace vs T_eff and knee, D strip, drag to move the threshold; fed by `DynamicsGroup::TakeMeter()` through a lock-free ring) and tooltips on every dynamics field. 7c done: spectrum analyzer (Pre/Post/Pre+Post, display tilt) behind the EQ curves. Then (D19): 8 bands; no Static Dyn Group (DynRange ≠ 0 subscribes a band; default groups in pairs; state v2 migration); group tabs grey when unused and show applied gain in yellow; DYN RANGE first in the band editor. Default 8-band EQ layout (HP and LP bypassed); LP12/HP12 nodes drag Q vertically. Band Shelf type (state v3 inserts it after High Shelf; B5 defaults to 500 Hz – 2 kHz) with edge handles on the graph. Option is a per-type integer (Vowel Weighting, LP/HP Slope; state v4 folds LP24_LR/HP24_LR into LP/HP). 7d (part): preset bar (factory + user presets, prev/next, modified `*`, Save / Save As / Rename / Show Folder; the state remembers the current preset) and undo/redo wired to every UI edit (preset loads are one step). Next: listen exclusivity, Spike B timing.
* **Not done yet:**
  * Phase 1's biquad comparison in the click test (only SVF boundedness is asserted).
* **Measured:** the worst case (10 heavy bands, 4 band-sum groups with K-weighted sidechains, 5 ms lookahead, 64-sample blocks, 48 kHz) costs **2.8% of one Apple M3 core** (before the cut to 8 bands), over the Phase 8 target of 2% on an M1. Optimization is deferred to Phase 8, except that the per-sample `log10` in the detectors gets replaced whenever that code is next touched. Other candidates: per-sample `a1` division and coefficient redesign during dynamic ramps.

Build and test:
```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build && (cd build && ctest)
tools/render/render_usecases.sh        # renders/*.wav + detector probe CSVs

scripts/setup-deps.sh                  # iPlug2 submodule, SDKs, patches
cmake -S plugin -B build-plugin -G Ninja -DCMAKE_BUILD_TYPE=Release -DEEQ_INSTALL_PLUGINS=ON
cmake --build build-plugin             # signs and installs to ~/Library/Audio/Plug-Ins
scripts/validate-plugins.sh            # auval, pluginval, clap-validator
```
(If `xcode-select -p` points at the Command Line Tools, prefix with `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer`.)

## Guiding approach
* **DSP first, offline, test-driven.** `dsp/` is a framework-free C++20 static library with no allocation, locks or I/O on the audio path. Every DSP feature lands with unit tests and an offline render before it is wired to a UI.
* **Thin wrapper.** `plugin/` (iPlug2) only maps parameters, I/O buses, state and UI onto the `dsp/` engine.
* **One source of truth for curves.** The UI's EQ curve is computed from the same coefficient code as the audio (analytic magnitude response), never re-derived separately.
* **Thread model.** Audio thread: process only. UI thread: drawing, FFT analysis, response curves. Communication: atomics for parameters, lock-free SPSC FIFOs for meter, analyzer and trigger data.

## Style

Use Google C++ style guide (two space indents, Google naming conventions, 80-column
line limit, etc.)

* Formatting: `.clang-format` at the repo root (`BasedOnStyle: Google`); run `clang-format -i` on changed files (Xcode ships one at `$(xcrun -f clang-format)`).
* Files: `lowercase_with_underscores.h` / `.cc`, tests as `*_test.cc`, `#define` include guards (`EVERYTHINGEQ_<PATH>_H_`), includes written from the repo root (`"dsp/svf/svf.h"`).
* Names: `PascalCase` types and functions, `snake_case` variables and struct fields, `trailing_underscore_` class members, `kConstant` constants and enumerators; cheap accessors are named like variables (`latency_samples()`). No `using namespace`.
* Exceptions: overrides of iPlug2 virtuals keep iPlug2's names (`ProcessBlock`, `OnIdle`, `Draw`, …), iPlug2-defined members keep theirs (`mLayoutFunc`, `mRECT`), and `plugin/config.h` and `plugin/resources/` follow iPlug2's conventions. Use-case JSON keys stay camelCase (data format).

## Repository layout
```
dsp/                 framework-free engine (C++20)
  svf/               ZDF SVF cell, coefficient design, response evaluation
  bands/             band types, polymorphic band slot, tap logic
  dynamics/          sidechain conditioning, detector, gain computer, ballistics
  engine/            chain, groups, routing/output modes, parameter snapshot
model/               framework-free plugin model: parameter table, slot mappings,
                     state, presets, undo history (depends on dsp/, nlohmann_json)
tests/               unit + property tests (Catch2), golden renders
tools/
  render/            CLI: wav in + param JSON → wav out (+ CSV of D_m, trigger)
  plot/              scripts to plot magnitude responses / envelopes (Python, dev only)
plugin/              iPlug2 project: maps model/ onto iPlug2 params, I/O, state
  ui/                IGraphics controls and layout
presets/factory/     factory presets (JSON, embedded at build time; also used by
                     the use-case renders)
scripts/             dependency setup, plugin validation, iPlug2 patches
.github/workflows/   CI
```

## Phase 0 — Foundations & spikes (de-risk first)
1. **Toolchain:** CMake (C++20) for `dsp/`, `tests/` and `tools/`; Catch2 via FetchContent. Add a pinned iPlug2 git submodule; build the plugin with iPlug2's project setup (evaluate its CMake support vs. its Xcode/VS project templates and pick one).
2. **Spike A (formats):** iPlug2 "hello gain" plugin with a sidechain input, built as AU, VST3 and CLAP on macOS (universal) and as VST3 and CLAP on Windows (CI only). Declare the stereo → stereo, mono → stereo and mono → mono layouts, each with a mono or stereo sidechain (D10). In Logic, verify that all three layouts are offered on the right track types and that the sidechain menu works. Confirm the state of iPlug2's CLAP support; if it falls short, fall back to clap-wrapper over the VST3 build. Optionally check VST3/CLAP sidechain routing in Reaper's free evaluation on macOS (D12).
3. **Spike B (UI):** IGraphics with NanoVG (D15): HiDPI and scaling, plus a 60 fps analyzer test at 960×560. Switch to Skia only if NanoVG can't meet that.
4. **CI:** GitHub Actions matrix (macos-latest universal, windows-latest), running the build, unit tests, `pluginval` (VST3), `auval` (macOS) and `clap-validator`. Builds are ad-hoc signed on macOS (D11). Upload the plugin bundles as workflow artifacts.

**Exit:** gain plugin loads in Logic in every layout, passes the validators on both platforms, and CI is green.

**Manual Logic checklist (Spike A):**
1. Logic finds "tulrich: EverythingEQ" under Audio Units → tulrich (if not, `killall -9 AudioComponentRegistrar` and restart Logic).
2. It can be inserted on a mono track (mono → mono and mono → stereo variants offered) and on a stereo track, and the layout readout in the UI matches.
3. The sidechain menu in the plugin header lists tracks/buses. With a sidechain selected, the Sidechain meters move; with "None", they stay at zero (the spike zeroes Logic's duplicate-main-input quirk).
4. Audio passes unchanged at 0 dB Output Gain, and Output Gain automation works. The In/SC/Out meters track the signal; pushing Output Gain past 0 dBFS latches the Out readout red until it's clicked or playback restarts.
5. Save, close and reopen the project: the Output Gain value is restored.

**Phase 0 findings:**
* iPlug2 now ships CMake support, CLAP and a sidechain example; the CMake build is used (no Xcode/VS project files).
* Three iPlug2 CLAP bugs are patched locally (`scripts/patches/iplug2-clap-fixes.patch`): wrong main-port flags in `audio-ports-config`, no parameter-value rescan after state load, and `latency_changed` called outside `activate()`. The latency fix matters for Lookahead in Phase 6. Phase 5 added two CLAP fixes (state saved through streams that accept partial writes; stepped-parameter text rounds the value) and `scripts/patches/iplug2-param-parse-func.patch`, which adds `IParam::SetParseFunc` so slot parameters can parse their own display text. Phase 7 added `scripts/patches/iplug2-process-while-bypassed.patch`: an opt-in `SetProcessWhileBypassed()` so host bypass (AU `BypassEffect`, VST3 bypass parameter) keeps calling `ProcessBlock()` and doesn't reset the AU; the plugin then crossfades (4 ms) instead of hard-switching to pass-through, and fades in from dry after a reset. Candidates for upstream PRs.
* CLAP SDK and clap-helpers are pinned to `main` commits, because current clap-helpers needs draft extensions newer than CLAP 1.2.x.
* `pluginval` on the AU warns that disabling the sidechain bus fails; harmless for Logic.
* `clap-validator` warns that random bytes load as state; Phase 5's versioned state fixes this. `auval` warns that the preset name isn't retained in class data; also Phase 5 (state chunks).

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
* Recombination identities from §3.D hold within 1e-5 (taps on the last band; mid-chain taps match $D(\text{main}) + \text{aux}$).
* Split M/S: L+R = Main and L−R = Aux exactly.
* Bypassed band equals the identity bit-exactly.

## Phase 4 — Dynamics engine (§3.B)
1. Sidechain conditioning: input selector, group SC filter, $x^2$, Link blend.
2. Layer 1: sigA (τ = 1 ms) and sigB (RMS Window) one-pole smoothers on power, then dB with a −70 dB floor.
3. α trigger and $T_{\text{eff}} = (1-\alpha)\,\text{Threshold} + \alpha\,\text{Rise}$; knee ramp; Above/Below mode.
4. Ballistics: attack / hold / release on the normalized scalar → $D_m$.
5. Per-band modulation for Gain (dB), Freq (oct) and Morph targets, with Global Scale and clamping. Modulation is evaluated at control rate and flows through the same interpolation as static changes.
6. Detector filters per band type, and the "Internal Band-Sum" path computed from the dry input (feed-forward guarantee).
7. Lookahead: an audio-path delay line (preallocated for 10 ms at the maximum sample rate) applied to the chain input and the dry, delta and bypass paths; detectors read the undelayed signals. The engine exposes `latencySamples()`.

**Tests:**
* α=0 on a stepped sine: trigger tracks level within 0.1 dB after settling.
* α=1 on a steady sine at −40 and −6 dBFS: trigger ≈ 0 dB at both. Transient bursts at different levels give the same peak trigger within 1 dB.
* Ballistics: measured attack/release times within 5% of setting; hold behaves as specified.
* Feed-forward: a boosting band subscribed to Internal Band-Sum cannot latch (bounded output over a 60 s noise render).
* Lookahead: with L ms, output equals the L-delayed output of the zero-lookahead engine fed the same sidechain advanced by L (within tolerance); a static configuration is exactly an L-sample delay of the 0 ms result; bypass and delta stay time-aligned; `latencySamples()` matches.
* Resonant sweep stress (touch-wah): Peak `+12 dB`, `Q = 6`, with Freq driven 20 Hz→0.45 fs by a 0–1 square-wave $D_m$ at attack 0.5 ms. Output stays bounded with no NaN, and the Vowel morph sweep across 0→4 gives continuous formant centers (no jumps at anchor boundaries).
* Use-case renders via `tools/render`: snare split, bass crossover, F-M loudness, vowel carving, dynamic HP, touch-wah (Peak + Vowel morph). Listen-check and save as golden files with tolerance-based regression comparison.

## Phase 5 — Parameters, state & presets (§3.E)
1. Parameter table (single declarative list, from the ranges table in ABOUT.md §3.E) generating iPlug2 params with IDs, ranges, skew, units and display formatters. X/Y/Z and DynRange are normalized host parameters, mapped to physical units per band type and per `Dyn Target` (D14); their display text and units follow the type. Tests: each mapping round-trips, and changing type keeps the normalized value.
2. Lock-free parameter snapshot to the engine each block; type and group changes are handled without allocation.
3. Engine: a mono mode for the mono → mono layout (`Output Mode` forced to `Stereo`; `Chan` and `Link` ignored), plus a mono sidechain copied to both detector channels. Implemented as parameter rules over the stereo engine fed dual-mono (D17); processing only one channel is a Phase 8 optimization. Tests: mono → mono output equals the left channel of the stereo engine fed a dual-mono signal with every `Chan` set to `Stereo`.
4. State serialization with version field and migration hooks; round-trip tests.
5. Factory presets covering the six use cases (touch-wah ships as two presets: Peak and Vowel) plus basic EQ starting points. User presets are saved as files in a per-user folder (preset JSON format; the in-plugin menu comes in Phase 7).
6. Undo/redo model: a bounded history of parameter edits made in the plugin UI (a drag counts as one step), kept on the UI thread. Host automation changes are not recorded. (Model done; wired to the UI in Phase 7.)

## Phase 6 — Plugin integration
1. I/O config: the three layouts from D10, each with a mono or stereo sidechain. Report latency from `latencySamples()` (0 by default), and update it when Lookahead or the sample rate changes; verify plugin delay compensation in Logic (including that a lookahead change takes effect after a transport restart). **Verified 2026-10-26:** a null test against an unprocessed copy breaks when Lookahead changes during playback and nulls again after stop/restart, so Logic picks up the new latency on transport restart. The plugin reports the new latency the moment the control moves (confirmed with the UI readout).
2. Per-format checks: AU with `auval` and manually in Logic; VST3 with `pluginval` and CLAP with `clap-validator`, in CI on macOS and Windows (D12).
3. Automation smoothness under Logic automation of every continuous parameter, plus an offline test that sweeps each normalized parameter at audio rate through the wrapper's parameter path.

## Phase 7 — UI (§4)
1. Layout skeleton at 960×560 with scaling (Spike B: confirm 60 fps for the analyzer and EQ graph at Retina scale with a debug-only frame timer); fixed slots; 35% opacity for inactive controls.
2. Band ribbon (selection, type, active state) and band editor row (type-specific X/Y/Z labels and units).
3. EQ graph: log-frequency grid, composite curve and per-band fills from `analyticResponse`, draggable nodes (drag = freq/gain, wheel = Q), plus the dynamic range shown as a ghost curve at $D_m = 1$ and a live curve at the current $D_m$.
4. Analyzer: Pre/Post/Pre+Post, FFT on the UI thread fed by a lock-free FIFO, display tilt.
5. Dynamics group panel: source and sidechain, detector and continuum, trigger meter with draggable threshold handle (showing Trigger vs $T_{\text{eff}}$), ballistics, `Grp Out` readout of $D_m$ in %.
6. Band editor `NOW` readout (the band's current dynamic offset in dB, oct or morph units); input and output level meters per ABOUT.md §4, using `plugin/ui/level_meter.h` (built during Phase 0; the audio thread only publishes per-block peaks through atomics, and ballistics run on the UI thread).
7. Graph node rules from ABOUT.md §4 (F-M pinned at 1 kHz, Vowel drags Morph/Gain, no node for K-Weight).
8. Global footer: Output Mode, Lookahead, Scale, Δ, Bypass. The preset bar (factory + user, save/rename) and Undo/Redo sit above the dynamics panel (done).
9. Listen exclusivity (D13): enabling SC Listen clears band Solos and vice versa; only one group's SC Listen at a time. Split output modes greyed in mono → mono.
10. UI polish (user wish list, 2026-10-27; not started):
    * Output Gain as a vertical slider beside the EQ graph, like Logic's stock EQ.
    * 0.5 dB detents on most dB parameters (0.1 dB steps are too fine to land on), with slower mouse response on those so a specific detent is easy to dial in. Detents done (ABOUT.md §4, Detents); the slower mouse response is still open.
    * Dials for most parameters.
    * Icons for band types, and elsewhere where they help.
    * Band ribbon: one click toggles a band on/off; Control-click (or click and hold) opens the type menu.
    * EQ graph modifiers: Command-drag on a node adjusts Gain only; other modifiers adjust other band params (to be assigned).
    * **Design challenge: make band ↔ group associations obvious at a glance.** Today the routing is invisible unless you open each band's Dyn rows and each group's panel. One easy mistake: a vowel-boost patch did nothing because its Level group was still on External Sidechain. The display should answer these without clicking:
      - Which groups drive this band, through which target (Gain/Freq/Morph), by how much, and in which direction?
      - Which bands does this group drive?
      - What does each group listen to (Input: Internal Dry, Band Sum, External SC, …) and how does it detect (Level / Vowel / Pitch)?
      - Is a group actually producing anything right now? Show live D, and flag silent sources, e.g. External SC selected with no sidechain connected.
      - Which combinations probably don't make sense, e.g. a Gain target on a Pitch group, or a Freq target on a Vowel group?

      Ideas to explore:
      - A color per group, reused on the group tabs, on the band nodes in the graph (a ring or tick per assignment), on the ribbon and in the Dyn rows.
      - Group tabs that carry a summary ("G2 Level · Dry", "G1 Vowel · SC") and a live D bar.
      - Hovering a group highlights its bands, and hovering a band highlights its groups.
      - A compact routing matrix (bands × groups, cells showing target and range), possibly as an alternate view of the dynamics panel.
      - Patch-cable lines between group tabs and graph nodes while a modifier is held.
      - Dragging a group chip onto a node to create an assignment.
      - A small patch bay (user idea, 2026-10-29): bands on one side, groups on the other, joined by curved wires drawn like patch cables (color = band, thickness or label = range, style = target; Dyn A and Dyn B as separate jacks). Drag from a band jack to a group jack to patch, drag off to unpatch. The same data could also be an 8 x 4 (or 16 x 4 with Dyn A/B rows) matrix, with lines or filled cells showing the connections; the wires read better at a glance, the matrix is more compact and easier to click.

      Wants real design effort (mockups, trying it on the Vocal Vowel Carve and drum band-shelf use cases) before any implementation. It also bears on the layout of the Dyn B row, which is a stopgap.
    * **Stack editing (user idea, 2026-10-29).** Holding a modifier shows all the bands as a faint stack of layers, one per band, in a sort of isometric projection over the band editor or graph. Any control you grab then moves the same control on every band in the stack, e.g. trim the Dyn Range of all eight Harmonic EQ bands at once. Changes are relative, so each band keeps its offset. Questions to settle:
      - Which bands join: all of them, only active ones (not Off or bypassed), only those of the same type, or only those on the same group? A per-band include toggle could be shown in the stack.
      - Whether the stack can be drawn legibly at 960 x 560, or whether a simpler cue does the job (all fields tinted, plus "x8" on the grabbed control).
      - How values clamp at their range limits, and whether the whole move is one undo step (it should be).
      - Copying settings between bands (user idea, 2026-10-29): select some of a band's controls (lasso them, or Shift-click / drag-select in the conventional way), then in the stack view Option-drag the selection onto other layers to copy those values there, e.g. B1's dynamics settings onto B2 and B3. Simpler fallbacks: band copy/paste of all settings or chosen sections (Type & shape, Dyn A, Dyn B, routing) from the ribbon's context menu, and Option-drag of one ribbon cell onto another. Copying should be one undo step. (ABOUT.md D15 left band copy/paste out of v1; revisit.)
      - Related: the global SCALE multiplies every band's Dyn A and Dyn B range. That covers "overall amount" for gain-only setups, but in Harmonic EQ it also scales the pitch-following Freq range (+8 oct), which detunes the bands. Consider letting SCALE apply only to Gain targets, or giving each assignment an "exempt from Scale" flag.
, edge handles, Morph target = width, state v3). Option is now generic (state v4: LP24_LR/HP24_LR folded into LP/HP with a Slope option); a Band Shelf transition slope could use it later.

## Phase 8 — Performance, QA & release
1. **CPU budget:** worst case (8 bands × 4 cells, 4 groups, stereo, 48 kHz, 64-sample buffers) under 2% of one Apple M1 core. Profile and optimize (SIMD across stereo channels) only if the budget is missed.
2. Edge cases: buffer size 1 and odd sizes, sample-rate changes, silence and DC, extreme parameters, rapid type switching.
3. Host testing: manual in Logic on macOS (all layouts, sidechain, lookahead/PDC, automation, presets, state recall across project reloads). VST3 and CLAP rely on the validators in CI; Windows has no manual testing.
4. Packaging (D11): a tag-triggered release workflow that builds and validates, then attaches zips to a GitHub Release. macOS: universal AU/VST3/CLAP bundles, ad-hoc signed, with install instructions (copy to `~/Library/Audio/Plug-Ins/...`, clear the quarantine flag with `xattr -dr com.apple.quarantine`, and run `killall -9 AudioComponentRegistrar` if Logic doesn't see the AU). Windows: VST3/CLAP zip. User manual derived from ABOUT.md, in the repo or GitHub Pages. If a Developer ID turns up later, add signing and notarization as an optional workflow step.

## Milestones
| Milestone | Contents | Demo |
| :--- | :--- | :--- |
| M0 | Phase 0 | Gain plugin with sidechain loads in Logic in all layouts; validators and CI green |
| M1 | Phases 1–3 | Offline renders of a static 10-band EQ with taps and output modes |
| M2 | Phase 4 | Offline renders of all six use cases |
| M3 | Phases 5–6 | Usable plugin with a generic UI in Logic; VST3/CLAP pass validators |
| M4 | Phase 7 | Full custom UI |
| M5 | Phase 8 | Release candidate |

## Risks
* **iPlug2 CLAP / sidechain maturity in specific hosts:** de-risked in Spike A; fallback is clap-wrapper.
* **Fletcher-Munson fit quality with only 3 cells:** if the fit error is too high, allow a 4th cell (the slot already supports 4).
* **Narrow dynamic vowel notches can modulate audibly ("zipper" or AM sidebands) at fast attack times:** mitigate with a minimum effective attack per band type, validated by listening tests.
* **Parameter count (~200):** check host automation-list usability; group names by band/group prefix.
* **Unsigned macOS builds:** Gatekeeper blocks quarantined downloads, which puts off less technical users. Mitigated by clear install notes; Developer ID notarization ($99/yr) fixes it outright if wanted later.
* **No manual testing outside Logic:** VST3/CLAP sidechain and layout behavior in other hosts is covered only by validators. Rely on user bug reports after release, and consider Reaper's free evaluation on macOS.

## Open items (not blocking M0–M2)
* Parameter names may be revisited. Logic sorts its automation menu alphabetically, so ID order only shows in its generic Controls view and in other hosts; numbered names were considered and rejected.
* Whether to expose `sigA` τ (D5) after listening tests.
* Formant Q values and F-M presence-bell center (tuning).
* Post-v1: multi-output buses for VST3/CLAP hosts, an instrument variant for Logic multi-out, formant tracking.
* Tracking detectors (user idea, 2026-10-27): **done** as group `Detect` = Vowel / Pitch (`dsp/dynamics/tracker.h`, ABOUT.md 3.B). A second Dyn assignment per band (Dyn B) is done too, in a fourth band-editor row for now (to be laid out properly in the UI polish pass); factory preset *Vocal Vowel Carve (SC)*.
* More tracking use cases (user ideas, 2026-10-28). Each could be written up in ABOUT.md §2 and get a factory preset:
  - **Talk box.** A vocal track feeds the sidechain, possibly while it's muted or pulled down in the mix. A Vowel band on a synth or guitar (positive Formant Gain, Dyn A Morph +4 on a Vowel group) follows the sung vowels. A Level group on the same sidechain, with Dyn B Gain, opens the band only while the singer is singing. Open questions:
    - Does Logic still send a muted track's audio to sidechains, or is a fader at −∞ or an unused output bus needed?
    - The band only moves along the OO–EE path with three formants, so consonants and nasals don't come through. A real talk box or vocoder imposes the whole spectral envelope. A vocoder-style band type (LPC envelope as the filter) is a possible later extension.
  - **Harmonic EQ.** Tie a band's frequency to the detected fundamental, or to a harmonic of it, to reshape timbre consistently as the note changes. For example, boost the 2nd harmonic for warmth, notch the 3rd for hollowness, or put a resonant BP on the 5th. This already works: on a Pitch group with Dyn Target Freq and DynRange +8 oct, a band at X = 27.5 × n Hz follows harmonic n. Factory preset *Harmonic EQ*: G1 tracks pitch, G2 level; band n is a Peak (Q 2n) on harmonic n via Dyn B (Freq +8 oct) and gets its boost/cut from DYN RANGE on G2, so a band is flat until its Dyn Range is turned up. To consider:
    - Expose "Harmonic n" directly instead of the 27.5 Hz arithmetic. This could be a per-band option or a display mode for X when the band follows a Pitch group.
    - Tracking only works on monophonic sources, and a glide or octave errors show up as audible jumps. Faster attack/release suits this better than the vowel defaults.
    - Several bands on one Pitch group could form a harmonic comb (e.g. odd harmonics only).
    - Extend the pitch tracker's 50 Hz floor down to about 30 Hz, so Harmonic EQ follows a bass's low E (41 Hz) and a 5-string's low B (31 Hz). YIN needs about two periods, so this means a longer window (~65 ms instead of ~43 ms), which adds tracking lag and CPU. Bass is a main Harmonic EQ use case (ABOUT.md use case 7).
