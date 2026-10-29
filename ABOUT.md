# Everything EQ - Dynamic Tone & Routing EQ — Architecture & Specification

## 1. Overview & Design Philosophy
A zero-latency, minimum-phase C++ audio plugin (built with **iPlug2**; targeting **AU** for Logic Pro, **VST3** on macOS and Windows, and **CLAP**) that unifies parametric EQ, psychoacoustic macro contours (Fletcher-Munson, broadband tilt, vowel formants), a continuous level-to-transient dynamics engine, and subtractive split routing inside a single utilitarian interface. Everything EQ is free and open source (Apache-2.0), distributed as unsigned builds on GitHub Releases (see D11).

**Core Principles:**
* **Zero-Latency Minimum Phase:** Uses Topology-Preserving Transform / Zero-Delay Feedback State Variable Filters (TPT/ZDF SVF) throughout. Avoids linear-phase FIR pre-ringing and latency while keeping audio-rate parameter modulation click-free. No oversampling. The only source of latency is the optional detector **Lookahead** (0–10 ms, default 0).
* **Decoupled Group Dynamics:** Up to 8 polymorphic filter bands can operate statically or move with one of 4 shared Dynamics Groups, each with independent per-band range scaling ($\pm\text{dB}$, $\pm\text{octaves}$, or $\pm$ vowel-morph index).
* **Unified Level/Transient Continuum:** A single parameter $\alpha \in [0, 1]$ morphs any detector from standard RMS/Peak level compression ($\alpha = 0$) to log-derivative transient/sustain detection ($\alpha = 1$).
* **Phase-Accurate Subtractive Split:** Any band can be tapped out of the main chain into a single **Aux** stream, using Linkwitz-Riley complements or algebraic subtraction (`x - y`), so that the outputs always recombine exactly (see §3.D for the precise identities).
* **Portable DSP Core:** All DSP lives in a framework-free C++20 library; iPlug2 is only the plugin/UI wrapper.

---

## 2. Major Mixing & Processing Use Cases

1. **Snare Attack vs. Ring Splitting (Single-Interface Drum Sculpting):**
   * *Setup:* Group 1 set to Transient Mode ($\alpha = 1.0$). Band 1 is a dynamic BP band (static gain at the `-60 dB` floor, which means silence, and DynRange `+60 dB`) at the stick-attack region, tapped to Aux; Band 2 shapes or notches the fundamental boxiness/ring on Main.
   * *Routing:* With Output Mode `Split L/R`, the transient stream arrives on the right channel and the residual body (`Dry - Attack`) on the left. Duplicate the track in the DAW, isolate each side (e.g. Logic's Direction Mixer / Gain plugin), and place clippers, gates, or reverbs on each before blending back phase-accurately.
2. **Bass Guitar Crossover & Adaptive Upper-Harmonic Fuzz/Growl:**
   * *Setup:* A dynamic Vowel or Peak band (Band 1) adds midrange articulation above the sub region; Band 2 is an HP band at 24 dB/oct (Linkwitz-Riley) at ~150 Hz tapped to Aux. Main carries the clean, LR-complementary low end; Aux carries the articulated highs.
   * *Routing:* In `Split L/R` mode, put fuzz/drive on the Aux side in the DAW, so the sub-bass stays free of intermodulation mud. (Bands placed *after* a tap process Main only, so the articulation band must come before the crossover.)
3. **Dynamic Fletcher-Munson (ISO 226) Loudness Compensation:**
   * *Setup:* A Fletcher-Munson band subscribed to a Dynamics Group using a K-weighted sidechain and a 300 ms RMS window (`Trigger Below Threshold` mode).
   * *Result:* Automatically brings up perceived low-end weight and high-air presence when a track or mix drops in volume, flattening out as the arrangement gets loud.
4. **Anti-Formant Lead Vocal Pocket Carving:**
   * *Setup:* Place the plugin on a dense rhythm guitar or keys bus. Set a band to `Vowel` mode (`/a/` or `/e/` profile) with **Flat** formant weighting and a negative Dynamic Range (`-4 dB`), triggered via external sidechain from the Lead Vocal.
   * *Result:* Instead of scooping a wide, lifeless 1–3 kHz hole out of the instruments, it dynamically notches only the three narrow formant needles where vocal intelligibility lives.
   * *Limitation:* Formant positions are static (or morph only via the dynamics signal); they do not track the singer's actual formants. Formant tracking is a post-v1 idea.
5. **Dynamic High-Pass / Low-Pass Filter Sweeps:**
   * *Setup:* Assign an HP or LP filter band to a Dynamics Group, with `Dyn Target = Freq` so `DynRange` modulates cutoff frequency in **octaves** rather than dB.
   * *Result:* Tightens rumble between notes or opens up top-end air solely on loud percussive transients.
6. **Touch-Wah / Envelope Filter (with Vowel-Sweep "Talking" Variation):**
   * *Setup (classic touch-wah):* On a guitar, clav, or bass track, a single resonant Peak band (`+12 dB`, `Q ≈ 6`) at ~400 Hz is subscribed to a Dynamics Group with `Dyn Target = Freq` and `DynRange = +2.5 oct`. The group uses `Internal Dry`, $\alpha \approx 0.3$ (mostly level-following, with extra kick on pick attacks), `Mode = Above`, a low threshold, and a wide knee (~20 dB) so that $D_m$ follows playing intensity smoothly rather than switching. Fast attack (~5 ms) and a slower release (~150 ms) give the characteristic "quack" and fall-back. For a darker auto-filter flavor, use a resonant LP band (12 dB/oct) instead of the Peak.
   * *Variations:* `Mode = Below` gives an inverted (downward) sweep. $\alpha = 1$ makes every note sweep by the same amount regardless of how hard it's played. An optional HP sidechain filter stops low notes from dominating the detector.
   * *Vowel-sweep ("talking wah") variation:* Replace the Peak with a `Vowel` band (positive Formant Gain, e.g. `+12 dB`, **Natural** weighting, `X = 0.0` /u/) and set `Dyn Target = Morph`. `DynRange = +2.0` sweeps "OO → OH → AH" ("wow"/"wah"); `+4.0` sweeps the full "OO → EE" axis ("you-ee"). Because the three formants move independently along the morph path, the result sounds vocal rather than like a single moving resonance. Tract Scale (Z) sets the apparent "size" of the voice.
   * *Tempo-synced auto-wah (sidechain trick):* There is no built-in LFO. Instead, set the group to `External SC` and sidechain from an instrument track that plays a pulse pattern (or a held note through a synth with a tempo-synced amplitude LFO), with that track's output routed to an unused bus so it isn't heard. Use $\alpha = 0$, `Mode = Above`, and set Threshold and a wide Knee to span the synth's level range, so that $D_m$ follows the pulse level. Attack/Release round off the motion. Use a carrier of ~1 kHz or higher (or noise) so the 1 ms fast tracker doesn't leave ripple. All groups share the single external sidechain input.
   * *Result:* Expressive, playing-dynamics-driven filter motion with zero latency, and no clicks or instability under fast resonant sweeps (the main reason for the ZDF SVF topology).
7. **Note-Tracking Bass EQ (Harmonic EQ):**
   * *Setup:* Start from the factory preset *Harmonic EQ*: G1 tracks pitch, G2 level, and band n is a Peak that sits on harmonic n of whatever note is playing. Turn band 1's `DynRange` down a little (e.g. `−3 dB`) to tame the fundamental, and bands 4–8 up (e.g. `+3` to `+6 dB`) to bring out the upper harmonics.
   * *Why:* For most bass notes the 4th harmonic and up land around 150–500 Hz, which carries the note on small speakers and is often where a bass EQ leaves a hole. A static boost there lifts a different harmonic on every note (the fundamental of one note, the 5th harmonic of another), so the timbre changes as the line moves. Because these bands follow the root, every note gets the same harmonic balance: the EQ responds to the bass line instead of to fixed frequencies.
   * *Limitation:* The pitch tracker reads 50 Hz – 1.5 kHz, so it doesn't follow the lowest notes of a bass (E1 is 41 Hz, and a 5-string's low B is 31 Hz).

---

## 3. Core DSP Architecture Notes

### A. Filter Engine: ZDF / TPT State Variable Filters
* **Topology:** Linear trapezoidal integrated State Variable Filters (Andrew Simper / Cytomic & Vadim Zavalishin TPT formulation).
* **Why ZDF SVF:** Solves the instantaneous feedback loop algebraically, so cutoff ($g = \tan(\pi f / f_s)$) and damping ($k = 1/Q$) remain orthogonal up to Nyquist, and the state variables stay meaningful when coefficients change every sample. Direct-form biquads add no latency either, but their state is tied to the coefficient set, so fast modulation causes zipper noise, transients, or instability. All shapes (LP/BP/HP/peak/shelf/notch) come from one cell as a linear mix of its outputs.
* **Polymorphic Band Slot:** Each of the 8 band slots wraps a small unit cascade (up to 4 SVF cells) configured by `Band_Type`:
  * *Primitives (Peak, Low/High Shelf, Notch, BP, and LP/HP at 12 dB/oct):* 1x SVF cell.
  * *Band Shelf:* a plateau of the band's gain between two edges, built from 2x Butterworth High Shelf cells (+gain at the low edge, −gain at the high edge) whose half-gain points are the edges. X is the center (the geometric mean of the edges), Z the width in octaves. Transitions are 12 dB/oct without overshoot; below about 2 octaves wide the plateau doesn't reach the full gain and the shape becomes a broad bell.
  * *LP/HP at 24 dB/oct (Linkwitz-Riley crossovers):* 2x cascaded SVF cells, both at the band's Q (default 0.7071 = LR4). LP and HP at 24 dB/oct, the same cutoff and Q 0.7071 sum to a flat-magnitude all-pass, **not** to the dry signal. Other Q values make a resonant 24 dB/oct filter.
  * *Broadband Tilt:* 4x High Shelf cells (Q 0.6, a quarter of the tilt each) at the midpoints of a span of `Width` octaves centered on the pivot $f_0$, with −tilt/2 of broadband gain: a monotonic ramp from −tilt/2 to +tilt/2 that is close to a straight dB-per-octave line across the width (within 0.25 dB through its middle half for widths of 4 octaves and up). Width 0 is a steep step at the pivot.
  * *Fletcher-Munson Contour:* 3x SVF cascade (Sub-Low Shelf < 100 Hz, Ear-Canal Presence Bell at 2.5–4 kHz, High-Air Shelf > 10 kHz) scaled proportionally by a single Phon Delta macro parameter, anchored to 0 dB at 1 kHz to prevent runaway volume jumps. This approximates the ISO 226:2003 equal-loudness difference curves, fitted offline (`tools/fmfit`, within ~0.15 dB at a 20-phon delta). Per ISO 226 the presence cell comes out as a small **cut** for positive phon deltas (about −1.2 dB at 2.6 kHz for 20 phon): at lower levels the ear is relatively *more* sensitive there.
  * *Vowel Formant:* 3x peaking SVF cells (see §3.C).
  * *K-Weighting Utility:* 2x fixed SVF stages (+4 dB high-shelf at ~1.68 kHz + 2nd-order RLB high-pass at 38 Hz), matching ITU-R BS.1770 within tolerance at all supported sample rates.
* **Efficiency & Nyquist Hygiene:**
  * Recalculate transcendental filter coefficients ($\tan$, $10^{\text{dB}/40}$) at a **control sub-block rate** (every 16 samples) with an epsilon dirty-check (`|ΔdB| > 0.005 dB`, or an equivalent relative frequency change), linearly interpolating $g$, $k$ and the output-mix weights across the sub-block when they are moving.
  * Use lightweight algebraic Q-prewarping near Nyquist to avoid high-shelf/bell cramping at 44.1/48 kHz without introducing oversampling latency.
  * Flush denormals (FTZ/DAZ) on the audio thread. Supported sample rates: 44.1–192 kHz.

### B. Dynamics & Detector Architecture
* **Feed-Forward Signal Flow:** Strictly feed-forward detector tapping: the raw dry input, the external sidechain, or a unit-gain sum of the subscribed bands' (those with a nonzero DynRange) detector filters applied to the **dry plugin input** (not the mid-chain signal). This prevents runaway positive feedback latching when bands are set to expand/boost, and lets the detectors run ahead of the audio when Lookahead is enabled.
* **Lookahead (global, 0–10 ms):** The audio path (dry input feeding the band chain, Main, Aux, Delta and Bypass paths) is delayed by `Lookahead` samples, while all detectors read the undelayed input and sidechain. The plugin reports this delay to the host as latency, so hosts with plugin delay compensation keep tracks (and the external sidechain) aligned. It is a single global value, because a plugin reports one latency. It is not automatable; latency changes take effect when the host re-queries (in some hosts, e.g. Logic, on transport restart). The new latency is reported at once, but in hosts that defer re-reading it (Logic and GarageBand) the audio path keeps the previous delay while the transport runs and switches on the next transport start, so the track never plays misaligned. At 0 ms there is no delay line and zero reported latency.
* **Tracking detectors (`Detect`):** `Level` is the trigger path below. `Vowel` and `Pitch` instead track the group's conditioned detector input (mono), and $D_m$ glides to the tracked value with Attack (upward) and Release (downward); Hold, Threshold, Rise, Knee, Mode, Alpha and RMS Window are unused (greyed). The input is low-passed and decimated to about 12 kHz and analyzed every ~5 ms over ~43 ms (`dsp/dynamics/tracker.h`); estimates update only while the input is voiced (YIN aperiodicity below 0.2) and above −50 dBFS RMS, otherwise the last value holds. Tracking lags by roughly 20–30 ms (Lookahead hides up to 10 ms). Use a clean single-voice sidechain; polyphonic input mostly reads as unvoiced and holds.
  * `Pitch`: YIN, 50 Hz – 1.5 kHz (within 10 cents on harmonic tones). $D = \log_2(f_0 / 27.5\,\text{Hz}) / 8$, so a band at 27.5 Hz with Dyn Target Freq and DynRange +8 oct sits on $f_0$ (55 Hz: the 2nd harmonic).
  * `Vowel`: LPC (order 12) envelope peaks give F1 and F2 (F1: the lowest peak within 12 dB of the strongest in 200 Hz – 1 kHz; F2: the strongest peak above it), projected onto the Vowel band's OO–OH–AH–EH–EE path in log frequency with F2 weighted ×4 (LPC pulls F1 toward a harmonic of higher voices); pairs far from the path count as unvoiced. $D = \text{Morph} / 4$, so a Vowel band at Morph 0 (OO) with Dyn Target Morph and DynRange +4 follows the sung vowel (within ±0.3 morph on synthesized vowels at 110–220 Hz). Use case: a vocal sidechain driving a Flat-weighted Vowel carve on the guitars. For Pitch, factory preset *Harmonic EQ* puts band n on harmonic n (Peak, Q 2n), so each band's DYN RANGE boosts or cuts that harmonic while a note plays (e.g. the 3rd and 4th harmonics of a bass).
* **Detector filter per band type** (used for `Internal Band-Sum` and band Solo):
  * Peak, Notch, BP, Vowel: unit-gain bandpass at the band's center frequency and Q (for Vowel, the sum of its three formant bandpasses).
  * Band Shelf: unit-gain bandpass at the center whose −3 dB points are the edges.
  * Low Shelf, LP: lowpass at the band's frequency. High Shelf, HP: highpass at the band's frequency.
  * Tilt, Fletcher-Munson, K-Weight: full-band (no filter).
* **Two-Layer Envelope + Log-Derivative Continuum:**
  1. **Sidechain Conditioning:** Input Selector (`Internal Dry`, `Internal Band-Sum`, `External SC`) $\rightarrow$ Optional Group SC Filter (`Off`, `BP`, `LP`, `HP`, `Tilt` (fixed +6 dB high-minus-low around SC Hz), `K-Weight`) $\rightarrow$ Power Rectification ($x^2$) $\rightarrow$ stereo combine using `Link` (0% = per-channel detection, 100% = max of channel powers, linear blend between).
  2. **Layer 1 (Pre-Threshold Symmetric Envelopes):** Run two 1-pole smoothers on $x^2$ in parallel—a fast tracker (`sigA`, fixed $\tau = 1$ ms in v1) and a slower RMS window tracker (`sigB`, $\tau$ = `RMS Window`, 1–400 ms)—and convert both to dB (clamped at a `-70 dB` noise floor to prevent silence chatter).
  3. **The $\alpha$ Continuum Trigger:**
     $$\text{Trigger}_{\text{dB}} = \text{sigA}_{\text{dB}} - \alpha \cdot \text{sigB}_{\text{dB}} = (1-\alpha)\,\text{sigA}_{\text{dB}} + \alpha\,(\text{sigA}_{\text{dB}} - \text{sigB}_{\text{dB}})$$
     * $\alpha = 0.0$: Pure level detection ($\text{sigA}_{\text{dB}}$).
     * $\alpha = 1.0$: Pure log-derivative transient detection ($\text{sigA}_{\text{dB}} - \text{sigB}_{\text{dB}}$, i.e. "dB of rise above the recent average"), giving consistent triggering across ghost notes and loud hits alike. A steady signal reads ≈ 0 dB.
     * $\alpha \in (0, 1)$: Program-adaptive "auto-riding" threshold biased toward spiky crest factors.
     * *Threshold Reference Scaling:* Two parameters, `Threshold` (dBFS, used at $\alpha=0$) and `Rise` (dB above the average, used at $\alpha=1$), are blended to match the decomposition above:
       $$T_{\text{eff}} = (1-\alpha)\cdot\text{Threshold} + \alpha\cdot\text{Rise}$$
       so sweeping $\alpha$ moves smoothly between the two without chasing the Threshold knob across a 30 dB DC shift. The UI greys `Rise` at $\alpha = 0$ and `Threshold` at $\alpha = 1$. The trigger meter and threshold handle display $\text{Trigger}_{\text{dB}}$ against $T_{\text{eff}}$.
  4. **Layer 2 (Gain Computer & Post-Threshold Ballistics):** Compare $\text{Trigger}_{\text{dB}}$ against $T_{\text{eff}}$, `Knee`, and `Trigger Mode (Above/Below)` to produce a raw normalized target scalar $\in [0.0, 1.0]$ (a smooth ramp across the knee width centered on $T_{\text{eff}}$; with Knee = 0, a hard 0/1 switch), then smooth with asymmetric `Attack`, `Hold`, and `Release` ballistics to emit the master Group Control signal $D_m \in [0.0, 1.0]$.
  5. **Per-Band Modulation:** Each band subscribed to Group $m$ applies `ActiveParam = StaticParam + (D_m * Band_DynRange) * Global_Scale` to the parameter chosen by its `Dyn Target` (Gain in dB, Freq in octaves, or Vowel Morph index; for Band Shelf and Tilt, Morph changes the width in octaves). Results are clamped to each parameter's legal range. A band can have a second assignment (`Dyn B`), applied the same way with its own group, target and range. With `Dyn Makeup` > 0 and a Gain target, the band also applies a broadband gain of $-\text{Makeup} \cdot D_m \cdot \text{DynRange} \cdot \text{Global\_Scale}$.

### C. Vowel & Formant Filter Outline
* **Acoustic Basis:** Models the first three vocal tract resonances: **$F_1$** (250–900 Hz, jaw openness), **$F_2$** (800–2500 Hz, tongue front/back), and **$F_3$** (2200–3200 Hz, brightness/timbre).
* **Preset Lookup Table (1D Morph Axis `0.0` to `4.0`):**

| Index | Vowel | Phonetic | $F_1$ (Hz) | $F_2$ (Hz) | $F_3$ (Hz) | Natural Weights ($F_1, F_2, F_3$) |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `0.0` | **OO** | `/u/` | 300 | 870 | 2250 | `0 dB, -12 dB, -24 dB` |
| `1.0` | **OH** | `/o/` | 450 | 1050 | 2600 | `0 dB, -9 dB, -20 dB` |
| `2.0` | **AH** | `/a/` | 730 | 1090 | 2440 | `0 dB, -4 dB, -18 dB` |
| `3.0` | **EH** | `/e/` | 530 | 1840 | 2480 | `0 dB, -6 dB, -16 dB` |
| `4.0` | **EE** | `/i/` | 270 | 2290 | 3010 | `0 dB, -14 dB, -18 dB` |

* **Parameters & Modulation:**
  * `X (Vowel Morph)`: Smoothly interpolates $\log(F_1), \log(F_2), \log(F_3)$ and the weights across the 5 vowel anchors.
  * `Y (Formant Gain)`: Peaking dB depth (positive = resonant formant boost; negative = anti-formant notch carving).
  * `Z (Tract Scale)`: Multiplies all three center frequencies by `0.70x` (baritone) to `1.40x` (alto/child).
  * `Option (Weighting)`: **Natural** applies $\text{gain}_i = Y \cdot 10^{w_i/20}$ (a realistic vocal spectrum; good for boosts). **Flat** applies $\text{gain}_i = Y$ to all three formants (needed for carving; Natural weights would leave a −4 dB carve at only ≈ −1 dB on F2 and ≈ −0.25 dB on F3).
  * Formant Q: fixed per formant (initial values $Q_1 = 5$, $Q_2 = 8$, $Q_3 = 10$; to be tuned by ear).
  * `DynRange`: Modulates Formant Gain ($\pm\text{dB}$) or sweeps the Vowel Morph index, per `Dyn Target`.

### D. Routing, Taps & Output Modes
v1 has **one main input, one optional sidechain input, and one output** in every format, with no multi-output buses. Logic Pro only offers multiple outputs to instrument plugins, so a Main/Aux pair is instead encoded into the single stereo output.

* **Supported layouts:** stereo → stereo, mono → stereo, and mono → mono. The sidechain may be mono or stereo in each layout (a mono sidechain feeds both detector channels).
  * *Mono → stereo:* the input is copied to both channels, then everything behaves as in stereo → stereo. Per-band `Chan` = `Side` (or `L`/`R` on identical channels) is legal but has the obvious effect.
  * *Mono → mono:* the engine runs one channel. `Output Mode` is forced to `Stereo` (Aux is summed back into Main) and the split modes are greyed; per-band `Chan` is ignored; `Link` has no effect.

* **Series chain:** Bands 1→10 process in series. A band with `Tap = Aux` splits its input $x$ into a Main part, which continues down the chain, and an Aux part, which is accumulated into the Aux stream. Bands after a tap process Main only.
* **Tap semantics by band category** ($y$ = the band's normal output for input $x$):
  * *Gain-type bands* (Peak, Shelves, Tilt, Fletcher-Munson, Vowel, K-Weight), **Delta tap:** $\text{Aux} \mathrel{+}= y - x$, $\text{Main} = x$. Aux carries exactly what the band adds or removes.
  * *Filter-type bands* (LP and HP at 12 dB/oct, BP, Notch), **Split tap:** $\text{Aux} \mathrel{+}= y$, $\text{Main} = x - y$.
  * *LP and HP at 24 dB/oct*, **Crossover tap:** $\text{Aux} \mathrel{+}= y$, $\text{Main} = $ the complementary LR output (flat-magnitude sum, all-pass phase).
  * *Recombination identities (unit-tested):* Downstream bands process only Main, so with a single tap at band $b$ and $D$ = the bands after it, the output is $\text{Main} + \text{Aux} = D(\text{main}_b) + \text{aux}_b$. In particular, when the tapped band is the **last** active band, $\text{Main} + \text{Aux}$ equals, to floating-point precision: for a delta tap, the untapped chain; for a split tap, the chain with that band removed; for an LR tap, the chain with that band replaced by its LR all-pass.
* **Output Mode** (global):
  * `Stereo` (default): stereo processing; Aux is summed back into Main (taps are inaudible).
  * `Split L/R`: input summed to mono; $L = \text{Main}$, $R = \text{Aux}$.
  * `Split M/S`: input summed to mono; $L = (\text{Main} + \text{Aux})/2$, $R = (\text{Main} - \text{Aux})/2$, so that $L + R = \text{Main}$ and $L - R = \text{Aux}$.
* **Per-band `Chan`** (`Stereo`, `L`, `R`, `Mid`, `Side`): applies only in `Stereo` output mode; ignored (mono) in the split modes.
* **Delta listen `[Δ]`:** outputs (processed − dry) so you hear only what the plugin changes.
* **Band Solo:** auditions the band's detector filter (§3.B) applied to that band's input.
* **Listen precedence** (highest wins): global `Bypass` (crossfaded) > group `SC Listen` (lowest-numbered group with it enabled; the conditioned sidechain, before Output Gain) > `Delta` (applied to whatever the chain outputs, including soloed bands) > band `Solo` (sum of all soloed bands). In the UI, enabling `SC Listen` clears all band Solos and vice versa, and only one group's `SC Listen` can be on at a time.

### E. Parameters & State
* A **fixed, versioned parameter list** (required for host automation):
  * Per band (×8): `Bypass, Type, Freq (X), Gain (Y), Q (Z), Dyn Group, Dyn Target, DynRange, Dyn Makeup, Dyn B Group, Dyn B Target, Dyn B Range, Option, Tap, Chan, Solo`. `Option` is a small integer whose meaning and display text depend on the band type (Vowel: Weighting; LP/HP: Slope), like the X/Y/Z slots.
  * Per group (×4): `Attack, Hold, Release, Threshold, Rise, Knee, Mode, Alpha, RMS Window, Input, Detect, SC Filter, SC Hz, SC Q, Link, SC Listen`.
  * Global: `Output Mode, Global Scale, Delta, Bypass, Output Gain, Lookahead` (Lookahead is not automatable).
  * Parameters are listed most-used first. Host names are fixed, so the X/Y/Z slots are named after their most common meaning, `Freq`, `Gain` and `Q`; for other band types the lane shows that type's value and units (e.g. a Vowel band's `Freq` lane reads "AH 2.00"; see the table below). The plugin UI shows type-specific names.
  * Global `Bypass` crossfades (4 ms) to the dry input (delayed by Lookahead). While fully bypassed the detectors keep running (so dynamics carry on through a short bypass) but the band chain is skipped; on release the band filters restart from reset state, at the detectors' current settings, under the crossfade. Use it rather than the host's bypass for click-free switching and automation: Logic switches third-party AUs in and out itself (it stops calling the plugin and never sets `kAudioUnitProperty_BypassEffect`), with fades that leave a hard edge. Hosts that do set the AU bypass property or the VST3 bypass parameter get the same 4 ms crossfade.
  * A band's `Bypass` keeps its settings and crossfades it out of the audio path (it still feeds an Internal Band-Sum detector and can be soloed); `Type = Off` means the slot is empty.
* **X/Y/Z meaning by band type:**

| Type | X (`Freq`) | Y (`Gain`) | Z (`Q`) | `Option` |
| :--- | :--- | :--- | :--- | :--- |
| Peak, Low Shelf, High Shelf | Freq (Hz) | Gain (dB) | Q | – |
| Band Shelf | Center (Hz) | Gain (dB) | Width (oct) | – (later: transition slope) |
| LP, HP | Freq (Hz) | – | Q | Slope: 12 / 24 dB/oct (24 = Linkwitz-Riley) |
| Notch | Freq (Hz) | – | Q | – |
| BP | Freq (Hz) | Gain (dB; `-60` = silence) | Q | – |
| Tilt | Pivot (Hz) | Tilt (dB, high minus low) | Width (oct) | – |
| Fletcher-Munson | – | Phon Delta | – | – |
| Vowel | Morph (0–4) | Formant Gain (dB) | Tract Scale (×) | Weighting: Natural / Flat |
| K-Weight | – | – | – | – |

* **Ranges and defaults.** The host sees X, Y, Z and DynRange as normalized 0–1 parameters whose mapping to physical units depends on the band type (and, for DynRange, on `Dyn Target`); the normalized value is what persists across type changes. Each mapping is piecewise (logarithmic for frequency, Q and Tract Scale, linear otherwise) with the slot's default at normalized 0.5, so the defaults below agree whatever the type: 1 kHz, 0 dB, Q 0.707, Width 2 oct, Morph 2 (AH), Tract 1.0×, DynRange 0. Dynamic modulation is clamped to the same ranges as the static value.

| Parameter | Range | Default | Skew / notes |
| :--- | :--- | :--- | :--- |
| Type | Off + 12 types | Off | LP/HP slope is the Option |
| Freq (X) | 20 Hz – 20 kHz | 1 kHz | log |
| Gain (Y) | ±30 dB (BP: −60 … +30) | 0 dB | linear |
| Q (Z) | 0.1 – 40 | 0.707 | log |
| Width (Z) | Band Shelf: 0.1 – 10 oct; Tilt: 0 – 10 oct | Band Shelf 2 oct; Tilt 4 oct | Band Shelf log; Tilt linear |
| Tilt (Y) | ±24 dB | 0 dB | |
| Phon Delta (Y) | ±40 phon | 0 | |
| Vowel Morph (X) | 0 – 4 | 2 (AH) | linear |
| Tract Scale (Z) | 0.70 – 1.40× | 1.0× | log |
| DynRange | Gain ±60 dB; Freq ±8 oct; Morph ±4 | 0 | linear, centered |
| Band layout | Type and Freq per band | B1 HP 12 dB/oct 30 Hz (bypassed), B2 Low Shelf 100 Hz, B3–B4 Peak 150 Hz / 500 Hz, B5 Band Shelf 500 Hz – 2 kHz (center 1 kHz, width 2 oct), B6 Peak 2 kHz, B7 High Shelf 4 kHz, B8 LP 12 dB/oct 8 kHz (bypassed) | flat (0 dB, Q 0.707) and transparent until edited; the Init preset matches |
| Dyn B Group / Target / Range | as Dyn Group / Target / Range | Group as Dyn Group, Gain, 0 | a second, independent assignment applied on top of the first (both on the same target add). E.g. a Vowel carve with Dyn A on a Vowel-tracking group (Morph +4) and Dyn B on a level group (Gain −6 dB): the carve follows the sung vowel and only cuts while the singer sings (factory preset *Vocal Vowel Carve (SC)*). |
| Dyn Makeup | 0 – 100% | 0% | with Dyn Target = Gain on Peak, Low/High Shelf, Band Shelf and Vowel: a broadband gain of −Makeup × the band's current dynamic gain. E.g. a midrange Band Shelf with DynRange −1.5 dB and Makeup 100% boosts everything outside the plateau by 1.5 dB on each hit instead of ducking the mids. |
| Dyn Group | G1–G4 | Bands 1–2: G1, 3–4: G2, 5–6: G3, 7–8: G4 | a band moves with its group only when DynRange ≠ 0 |
| SC Hz / SC Q | 20 Hz – 20 kHz / 0.1 – 40 | 1 kHz / 0.707 | log |
| RMS Window | 1 – 400 ms | 15 ms | log |
| Alpha | 0 – 1 | 0 | |
| Detect | Level / Vowel / Pitch | Level | see *Tracking detectors* |
| Link | 0 – 100% | 100% | |
| Threshold | −70 – 0 dBFS | −20 dBFS | |
| Rise | 0 – +30 dB | +6 dB | |
| Knee | 0 – 30 dB | 0 dB | |
| Attack / Hold / Release | 0.1 – 500 ms / 0 – 500 ms / 1 – 5000 ms | 5 / 0 / 100 ms | log (Hold linear) |
| Global Scale | −100% – +200% | 100% | negative inverts every band's DynRange |
| Output Gain | ±24 dB | 0 dB | |
| Lookahead | 0 – 10 ms | 0 ms | not automatable |
* Inactive parameters keep their values when a band's type changes.
* State is saved through the host's plugin state as versioned JSON keyed by stable parameter names (`band3.x`, `group1.attack_ms`, …), with migration for older versions; unknown keys are ignored and missing ones get defaults.
* Presets are JSON files (`.eeqpreset`) holding a name and the parameters in physical units (Hz, dB, ms), so they read and diff well. Factory presets ship inside the plugin (one per use case in §2, with touch-wah as Peak, Vowel and sidechain variants, plus Init and a basic 4-band EQ) and appear in the host's preset menu. User presets are saved to `~/Library/Application Support/Everything EQ/Presets` (macOS) or `%LOCALAPPDATA%\Everything EQ\Presets` (Windows). The plugin state remembers which preset the settings came from.

---

## 4. UI Wireframe & Layout Specification

Window target: **`960 x 560 px`** (scalable 75%–200%), single-screen utilitarian layout. Inactive controls stay in fixed slots at **35% opacity** to preserve spatial memory.

**Legend:** `(M)` beside the graph = output level meter with peak readout; `NOW` = the selected band's current dynamic offset in its target's unit; `Grp Out` = the selected group's $D_m$ as a percentage; `(g)` = may be greyed (35%) depending on other settings (e.g. SC Hz/Q under K-Weight); `(M)` = read-only meter/readout; `*` = selected; `[Δ]` = delta listen; `SCALE` = Global_Scale applied to every band's DynRange; analyzer `Tilt` = display-only slope (dB/oct) for the spectrum analyzer (default 0).

```text
+--------------------------------------------------------------------------------------------------+
| [B1: HP] [B2: Peak*] [B3: Tilt] [B4: F-M] [B5: Vowel] [B6: --]...  | [<][>][ Init *  v ][UNDO][REDO] | <- RIBBON, PRESET BAR
+-----------------------------------------------------------------------+--------------------------+
|  TYPE: [ Vowel Formant v ]   DYN GRP: [ Grp 1 v ]   TAP: [ Main v ]   |  DYNAMICS GROUP          |
|  X (Morph): [ AH (2.0) ]     Y (Gain): [ -3.0 dB ]  Z (Tract): [1.0x] |  [G1*] [G2] [G3] [G4]    |
|  DYN RANGE: [ +6.5 dB  ]     CHAN:     [ Stereo v]  [BYP] [SOLO]      |--------------------------|
|  DYN TGT:   [ Gain v   ]     OPTION:   [ Flat v  ]  NOW: +4.0 dB (M)  |  SOURCE & SIDECHAIN      |
+-----------------------------------------------------------------------+  Input:  [ Int. Band v ] |
| +18 dB                                                         | +12  |  SC Flt: [ K-Weight  v ] |
|        .       .         .        .         .         .        |      |  SC Hz:  [ 1000 Hz ] (g) |
| +12 dB .       .    (2)  .        .    _    .         .        |  +6  |  SC Q:   [ 0.71    ] (g) |
|        .       .   /   \ .        .   / \   .         .        |      |  [ SC LISTEN ]           |
|  +6 dB .       .  /▒▒▒▒▒\.        .  /▒▒▒\  .         .   (3)  |   0  |--------------------------|
|        .  (1)  . /▒▒▒▒▒▒▒\        . /▒▒▒▒▒\ .         .  .---  |  dB  |  DETECTOR & CONTINUUM    |
|   0 dB ---/-----*---------*--------*-------*----------*-/----- |      |  RMS Win:[ 15.0 ms ]     |
|        . /     .           \      /         \         ./       |  -6  |  Alpha:  [ 0.65 (Trans)] |
|  -6 dB ./      .         .  \____/           \________/        |      |  Link:   [ 100 %   ]     |
|        /       .         .        .         .         .        | -12  |                          |
| -12 dB         .         .    ░░░░░░░░░░░░░░░░░░░░░   .        |      |  TRIG METER & THRESHOLD  |
|     ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ | -24  |  [||]  Thresh: -14.2 dB  |
| -18 dB ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ |      |  [||]  Rise:   +6.0 dB(g)|
|     20Hz     50Hz     100Hz    200Hz    500Hz    1kHz    5kHz  | [0.0]|  [██]  Knee:    4.0 dB   |
+----------------------------------------------------------------+------+  [██] <--Thresh Handle   |
|  ANALYZER: [Pre+Post v]  Tilt: [4.5dB]  | SCALE: [100%] [Δ] [BYPASS]  |  [██]  Mode: [Above v]   |
|  OUTPUT: [Stereo v]  LOOKAHEAD: [0.0 ms]                              |--------------------------|
+-----------------------------------------------------------------------+  BALLISTICS & ROUTING    |
                                                                        |  Attack: [  3.5 ms ]     |
                                                                        |  Hold:   [  0.0 ms ]     |
                                                                        |  Release:[ 85.0 ms ]     |
                                                                        |  Grp Out:[  62 %   ] (M) |
                                                                        +--------------------------+
```

* **Graph nodes:** frequency bands drag freq (horizontal) and gain (vertical), with the wheel adjusting Q. LP and HP nodes sit on the curve at the cutoff (Q in dB, doubled at 24 dB/oct) and drag Q vertically, as in Logic's Channel EQ. Fletcher-Munson has a node pinned at 1 kHz whose vertical drag sets Phon Delta. Vowel's node drags Morph (horizontal) and Formant Gain (vertical). K-Weight has no node. When a band's dynamics move its frequency away from its node (e.g. Dyn Target Freq following pitch), a thin line in the band's color marks its live frequency, with a dot on the live curve and the band number at the top. A selected Band Shelf also shows handles at its two edges; dragging one moves that edge while the other stays put (the wheel on its node changes the width). Option-dragging a node sets its `DynRange`, and Cmd-dragging (Ctrl on Windows) its `Dyn B Range`, along the axis its target moves: vertical for Gain, horizontal for Freq and Morph. The selected band shows where each assignment takes it at full dynamics ($D = 1$) as a ring labeled A or B, joined to its node.
* **Group meters:** each group tab has a strip along its bottom: the group's live $D_m$ in blue (grey, with a dimmed label, while no active band has the group and a nonzero DynRange), and below it, in yellow, the gain the group's bands apply right now, growing from the center (left for a cut, right for a boost; full width = 24 dB). The applied gain is the largest deviation of those bands' combined response from their static response (a silent band counts as −60 dB). The detector scope repeats it as a number beside D.
* **Level meters:** a vertical input meter at the left edge of the EQ graph and the output meter at its right (the `(M)` column), one bar per channel. Scale −60 to +6 dBFS (float signals can exceed full scale), segmented: green below −12 dBFS, yellow from −12 to 0, red above 0, with a peak-hold tick. Above each meter, a numeric readout of the recent peak (the meter's name sits below it); once a peak exceeds 0 dBFS the readout latches in red, showing the highest peak, until it is clicked or the host transport starts. Inactive channels (e.g. R on a mono track) are drawn empty.
* **Presets & editing:** the preset bar above the dynamics panel works alongside the host's own preset handling. Arrows step through the factory presets and then the user presets; the name shows the current preset, with `*` once the settings differ from it (comparing only what a preset stores), and opens the menu: factory presets, user presets, Save (the current user preset), Save As..., Rename... (names are typed in place; saving over another user preset asks first) and Show Presets Folder. User presets are plain files, so they can be copied between machines and hosts. Loading a preset is one undo step. UNDO / REDO (and Command-Z / Shift-Command-Z when the host passes keys to the plugin) undo edits made in the plugin UI; a drag is one step, host automation isn't recorded, and the history is cleared when the host restores a state (project load, its own preset menu). No A/B comparison or band copy/paste in v1.
* **Graphics:** IGraphics with the NanoVG backend.

---

## 5. Decision Log

| # | Decision | Rationale |
| :--- | :--- | :--- |
| D1 | Framework: **iPlug2**; formats AU (macOS), VST3 (macOS + Windows), CLAP. | Permissive license compatible with this repo's Apache-2.0; JUCE 8 would force AGPL or a commercial license. |
| D2 | No multi-output buses in v1; Main/Aux encoded into stereo via `Split L/R` or `Split M/S`. | Logic Pro offers multiple outputs only to instrument plugins; one bus layout works in every host. |
| D3 | Explicit tap semantics (delta / split / LR crossover) with tested recombination identities. | "Dry − Band" was undefined for gain-type and mid-chain bands; LR complements sum to an all-pass, not to dry. |
| D4 | `Threshold` + `Rise` blended by α. | Scaling a single threshold by (1−α) left the α = 1 threshold with no sensitivity control. |
| D5 | `sigA` fixed at τ = 1 ms in v1. | It was unspecified; exposing it can come later. |
| D6 | Vowel `Option`: Natural / Flat weighting; Z = Tract Scale only; fixed per-formant Q. | Natural weights made use case 4 (three-notch carving) impossible, and one knob controlling both scale and Q was ambiguous. |
| D7 | Per-band `Dyn Target` and `Option` parameters. | DynRange needs a defined unit per band (dB / octaves / morph index). |
| D8 | DSP in a framework-free C++20 library. | Unit-testable offline; insulates the DSP from wrapper choices. |
| D9 | Global, non-automatable detector Lookahead (0–10 ms, default 0), implemented as an audio-path delay reported to the host; Internal Band-Sum detection reads the dry input. | Lets dynamics catch transients before they arrive; one global value because a plugin reports one latency; default 0 preserves zero latency. |
| D10 | I/O layouts: stereo → stereo, mono → stereo, mono → mono; mono or stereo sidechain. Mono → mono forces `Stereo` output mode. | Logic users often insert plugins on mono tracks; split modes need two output channels. |
| D11 | Free and open source; unsigned builds (ad-hoc signed on macOS) on GitHub Releases, with install notes for clearing the macOS quarantine flag. Developer ID signing and notarization are optional later. | No signing certificates are available; ad-hoc signing is enough for Apple Silicon to load the plugins locally. |
| D12 | Manual host testing in Logic Pro (AU) on macOS only. VST3 and CLAP are covered by `pluginval` and `clap-validator` in CI on macOS and Windows; Reaper's free evaluation on macOS is an optional extra check for VST3/CLAP sidechain routing. | That is the hardware and software available. |
| D13 | Listen precedence: Bypass > SC Listen > Delta > Solo; SC Listen and Solo are mutually exclusive in the UI. | Makes simultaneous listen modes well defined. |
| D14 | X/Y/Z and DynRange are normalized host parameters with per-type (per-target) mappings, named `Freq`/`Gain`/`Q` for hosts. | Keeps the parameter list fixed (186 params) while one slot can hold any band type; EQ names are the most common meaning and read better than X/Y/Z. |
| D15 | UI: in-plugin presets and undo/redo; no A/B or copy/paste; NanoVG backend; `Grp Out` shows $D_m$ in %. | Scope control for v1; NanoVG is lighter and simpler to build than Skia. |
| D16 | Slot mappings are piecewise with every default at normalized 0.5. | Keeps the spec's defaults consistent across band types, since the normalized value persists when the type changes. |
| D17 | State and presets are JSON: state keyed by stable parameter names, presets in physical units. Mono → mono runs the stereo engine on dual-mono input with mono rules applied to the parameters. | Robust versioning and readable presets; the mono rules need no separate engine path (a mono engine is a Phase 8 CPU optimization if needed). |
| D18 | In Logic/GarageBand, a Lookahead change reaches the audio path on the next transport start; elsewhere at once. | Those hosts re-read AU latency only on transport start; holding the old delay until then keeps compensation correct throughout. |
| D19 | 8 bands. Dyn Group has no "Static" choice: every band has a group (default pairs: bands 1–2 → G1 … 7–8 → G4) and subscribes to it only when DynRange ≠ 0. State version 2 migrates v1 Static bands to their default group with DynRange 0. | Turning on dynamics is one control (DynRange) instead of three; 8 bands fit the ribbon and cover the use cases, and a second instance covers more. |
