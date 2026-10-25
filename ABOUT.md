# Everything EQ - Dynamic Tone & Routing EQ — Architecture & Specification

## 1. Overview & Design Philosophy
A zero-latency, minimum-phase C++ audio plugin (targeting Logic Pro AU and VST3 via JUCE) that unifies parametric EQ, psychoacoustic macro contours (Fletcher-Munson, broadband tilt, vowel formants), a continuous level-to-transient dynamics engine, and subtractive multi-output crossover routing inside a single utilitarian interface.

**Core Principles:**
* **Zero-Latency Minimum Phase:** Uses Topology-Preserving Transform / Zero-Delay Feedback State Variable Filters (TPT/ZDF SVF) throughout. Avoids linear-phase FIR pre-ringing and latency while keeping audio-rate parameter modulation click-free.
* **Decoupled Group Dynamics:** Up to 10 polymorphic filter bands can operate statically or subscribe to 4 shared Dynamics Groups, each with independent per-band range scaling ($\pm\text{dB}$ or $\pm\text{octaves}$).
* **Unified Level/Transient Continuum:** A single parameter $\alpha \in [0, 1]$ morphs any detector from standard RMS/Peak level compression ($\alpha = 0$) to threshold-free log-derivative transient/sustain detection ($\alpha = 1$).
* **Phase-Accurate Subtractive Taps:** Supports auxiliary DAW output buses using Linkwitz-Riley crossovers or algebraic subtractive splitting (`Dry - Band`) so split streams sum back to unity with zero phase cancellation.

---

## 2. Major Mixing & Processing Use Cases

1. **Snare Attack vs. Ring Splitting (Single-Interface Drum Sculpting):**
   * *Setup:* Group 1 set to Transient Mode ($\alpha = 1.0$). Band 1 shapes the stick attack; Band 2 shapes or notches the fundamental boxiness/ring.
   * *Routing:* Option to split the transient stream to `Aux Tap A (Out 3/4)` and the residual body (`Dry - Attack`) to `Main (Out 1/2)` so external clippers, gates, or reverbs can be placed on discrete DAW tracks and blended back phase-accurately.
2. **Bass Guitar Crossover & Adaptive Upper-Harmonic Fuzz/Growl:**
   * *Setup:* Band 1 acts as an LR24 or subtractive low-pass crossover at ~150 Hz, keeping sub-bass clean and mono on `Main`. High frequencies tap off to `Aux Tap A` (or upper bands) where a dynamic Vowel or Peaking group adds aggressive midrange articulation without intermodulation mud in the sub-bass.
3. **Dynamic Fletcher-Munson (ISO 226) Loudness Compensation:**
   * *Setup:* A Fletcher-Munson band subscribed to a Dynamics Group using a K-weighted sidechain and a 300 ms RMS window (`Trigger Below Threshold` mode).
   * *Result:* Automatically brings up perceived low-end weight and high-air presence when a track or mix drops in volume, flattening out as the arrangement gets loud.
4. **Anti-Formant Lead Vocal Pocket Carving:**
   * *Setup:* Place the plugin on a dense rhythm guitar or keys bus. Set a band to `Vowel` mode (`/a/` or `/e/` profile) with a negative Dynamic Range (`-4 dB`), triggered via external sidechain from the Lead Vocal.
   * *Result:* Instead of scooping a wide, lifeless 1–3 kHz hole out of the instruments, it dynamically notches only the three narrow formant needles where vocal intelligibility lives.
5. **Dynamic High-Pass / Low-Pass Filter Sweeps:**
   * *Setup:* Assign an HP or LP filter band to a Dynamics Group, where `DynRange` modulates cutoff frequency in **octaves** rather than dB.
   * *Result:* Tightens rumble between notes or opens up top-end air solely on loud percussive transients.

---

## 3. Core DSP Architecture Notes

### A. Filter Engine: ZDF / TPT State Variable Filters
* **Topology:** Linear trapezoidal integrated State Variable Filters (Andrew Simper / Cytomic & Vadim Zavalishin TPT formulation).
* **Why ZDF SVF:** Solves the feedback loop algebraically before discretization, avoiding the 1-sample delay ($z^{-1}$) artifact of standard biquads. Cutoff frequency ($g = \tan(\pi f / f_s)$) and resonance/damping ($k = 1/Q$) remain orthogonal up to Nyquist and can be modulated rapidly without clicks or instability.
* **Polymorphic Band Slot:** Each of the 10 band slots wraps a small unit cascade (up to 3–4 SVF cells) configured by `Band_Type`:
  * *Primitives (Peak, Low/High Shelf, LP12, HP12, Notch, BP):* 1x SVF cell.
  * *Linkwitz-Riley Crossovers (LP24_LR, HP24_LR):* 2x cascaded Butterworth SVF cells ($Q = 0.7071$).
  * *Broadband Tilt:* 1x Low Shelf + 1x High Shelf pivoted inversely around center frequency $f_0$.
  * *Fletcher-Munson Contour:* 3x SVF cascade (Sub-Low Shelf < 100 Hz, Ear-Canal Presence Bell at 2.5–4 kHz, High-Air Shelf > 10 kHz) scaled proportionally by a single Phon Delta macro parameter, anchored to 0 dB at 1 kHz to prevent runaway volume jumps.
  * *K-Weighting Utility:* 2x fixed biquad/SVF stages (+4 dB high-shelf at ~1.68 kHz + 2nd-order RLB high-pass at 38 Hz).
* **Efficiency & Nyquist Hygiene:**
  * Recalculate transcendental filter coefficients ($\tan$, $10^{\text{dB}/40}$) at a **control sub-block rate** (every 8 or 16 samples) with an epsilon dirty-check (`|ΔdB| > 0.005 dB`), interpolating state weights if moving rapidly.
  * Use lightweight algebraic Q-prewarping near Nyquist to avoid high-shelf cramping at 44.1/48 kHz without introducing oversampling latency.

### B. Dynamics & Detector Architecture
* **Feed-Forward Signal Flow:** Strictly feed-forward detector tapping (tapping either raw input, external sidechain, or a unit-gain bandpass sum of subscribed bands *prior* to dynamic gain modulation) to prevent runaway positive feedback latching when bands are set to expand/boost.
* **Two-Layer Envelope + Log-Derivative Continuum:**
  1. **Sidechain Conditioning:** Input Selector (`Internal Dry`, `Internal Band-Sum`, `External SC`) $\rightarrow$ Optional Group SC Filter (`Off`, `BP`, `LP`, `HP`, `Tilt`, `K-Weight`) $\rightarrow$ Power Rectification ($x^2$).
  2. **Layer 1 (Pre-Threshold Symmetric Envelopes):** Run two 1-pole smoothers in parallel—a fast tracker (`sigA`) and a slower RMS window tracker (`sigB`, controlled by `RMS Window` up to 400 ms)—and convert both to dB (clamped at a `-70 dB` noise floor to prevent silence chatter).
  3. **The $\alpha$ Continuum Trigger:**
     $$\text{Trigger}_{\text{dB}} = \text{sigA}_{\text{dB}} - \alpha \cdot \text{sigB}_{\text{dB}}$$
     * $\alpha = 0.0$: Pure level detection ($\text{sigA}_{\text{dB}}$).
     * $\alpha = 1.0$: Pure log-derivative transient detection ($\text{sigA}_{\text{dB}} - \text{sigB}_{\text{dB}}$), providing threshold-free trigger consistency across ghost notes and loud hits alike.
     * $\alpha \in (0, 1)$: Program-adaptive "auto-riding" threshold biased toward spiky crest factors.
     * *Threshold Reference Scaling:* Scale the effective threshold offset by $(1 - \alpha)$ so sweeping $\alpha$ from `0` to `1` does not require chasing the Threshold knob across a 30 dB DC shift.
  4. **Layer 2 (Gain Computer & Post-Threshold Ballistics):** Compare $\text{Trigger}_{\text{dB}}$ against `Threshold`, `Knee`, and `Trigger Mode (Above/Below)` to produce a raw normalized target scalar $\in [0.0, 1.0]$, then smooth with asymmetric `Attack`, `Hold`, and `Release` ballistics to emit the master Group Control signal $D_m \in [0.0, 1.0]$.
  5. **Per-Band Modulation:** Each band subscribed to Group $m$ applies `ActiveParam = StaticParam + (D_m * Band_DynRange) * Global_Scale`.

### C. Vowel & Formant Filter Outline
* **Acoustic Basis:** Models the first three vocal tract resonances: **$F_1$** (250–900 Hz, jaw openness), **$F_2$** (800–2500 Hz, tongue front/back), and **$F_3$** (2200–3200 Hz, brightness/timbre).
* **Preset Lookup Table (1D Morph Axis `0.0` to `4.0`):**

| Index | Vowel | Phonetic | $F_1$ (Hz) | $F_2$ (Hz) | $F_3$ (Hz) | Relative Weights ($F_1, F_2, F_3$) |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `0.0` | **OO** | `/u/` | 300 | 870 | 2250 | `0 dB, -12 dB, -24 dB` |
| `1.0` | **OH** | `/o/` | 450 | 1050 | 2600 | `0 dB, -9 dB, -20 dB` |
| `2.0` | **AH** | `/a/` | 730 | 1090 | 2440 | `0 dB, -4 dB, -18 dB` |
| `3.0` | **EH** | `/e/` | 530 | 1840 | 2480 | `0 dB, -6 dB, -16 dB` |
| `4.0` | **EE** | `/i/` | 270 | 2290 | 3010 | `0 dB, -14 dB, -18 dB` |

* **Parameters & Modulation:**
  * `Param1 (Vowel Morph)`: Smoothly interpolates $\log(F_1), \log(F_2), \log(F_3)$ across the 5 vowel anchors.
  * `Param2 (Formant Gain)`: Scales the peaking dB depth (positive = resonant formant boost; negative = anti-formant notch carving).
  * `Param3 (Tract Scale / Q)`: Multiplies all three center frequencies by `0.70x` (baritone) to `1.40x` (alto/child) and sets resonance width.
  * `DynRange`: Can modulate either Formant Gain ($\pm\text{dB}$) or sweep the Vowel Morph index dynamically on transients.

---

## 4. UI Wireframe & Layout Specification

Window target: **`960 x 560 px`**, single-screen utilitarian layout. Inactive controls stay in fixed slots at **35% opacity** to preserve spatial memory.

```text
+--------------------------------------------------------------------------------------------------+
| [B1: HP] [B2: Peak*] [B3: Tilt] [B4: F-M] [B5: Vowel] [B6: --] [B7: --] [B8: --] [B9: --] [B10]  | <- TOP BAND RIBBON
+-----------------------------------------------------------------------+--------------------------+
|  TYPE: [ Vowel Formant v ]   DYN GRP: [ Grp 1 v ]   TAP: [ Main v ]   |  DYNAMICS GROUP          |
|  X (Morph): [ AH (2.0) ]     Y (Gain): [ -3.0 dB ]  Z (Q/Sz): [ 1.0x ]|  [G1*] [G2] [G3] [G4]    |
|  DYN RANGE: [ +6.5 dB  ]     CHAN:     [ Stereo v]  [BYP] [SOLO]      |--------------------------|
+-----------------------------------------------------------------------+  SOURCE & SIDECHAIN      |
| +18 dB                                                         | +12  |  Input:  [ Int. Band v ] |
|        .       .         .        .         .         .        |      |  SC Flt: [ K-Weight  v ] |
| +12 dB .       .    (2)  .        .    _    .         .        |  +6  |  SC Hz:  [ 1000 Hz ] (g) |
|        .       .   /   \ .        .   / \   .         .        |      |  SC Q:   [ 0.71    ] (g) |
|  +6 dB .       .  /▒▒▒▒▒\.        .  /▒▒▒\  .         .   (3)  |   0  |  [ SC LISTEN ]           |
|        .  (1)  . /▒▒▒▒▒▒▒\        . /▒▒▒▒▒\ .         .  .---  |  dB  |--------------------------|
|   0 dB ---/-----*---------*--------*-------*----------*-/----- |      |  DETECTOR & CONTINUUM    |
|        . /     .           \      /         \         ./       |  -6  |  RMS Win:[ 15.0 ms ]     |
|  -6 dB ./      .         .  \____/           \________/        |      |  Alpha:  [ 0.65 (Trans)] |
|        /       .         .        .         .         .        | -12  |  Link:   [ 100 %   ]     |
| -12 dB         .         .    ░░░░░░░░░░░░░░░░░░░░░   .        |      |                          |
|     ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ | -24  |  TRIG METER & THRESHOLD  |
| -18 dB ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ |      |  [||]  Thresh: -14.2 dB  |
|     20Hz     50Hz     100Hz    200Hz    500Hz    1kHz    5kHz  | [0.0]|  [||]  Knee:    4.0 dB   |
+----------------------------------------------------------------+------+  [██] <--Thresh Handle   |
|  ANALYZER: [Pre+Post v]  Tilt: [4.5dB]  | SCALE: [100%] [Δ] [BYPASS]  |  [██]  Mode: [Above v]   |
+-----------------------------------------------------------------------+  [██]                    |
                                                                        |--------------------------|
                                                                        |  BALLISTICS & ROUTING    |
                                                                        |  Attack: [  3.5 ms ]     |
                                                                        |  Hold:   [  0.0 ms ] (g) |
                                                                        |  Release:[ 85.0 ms ]     |
                                                                        |  Grp Out:[ -2.4 dB ] (M) |
                                                                        +--------------------------+