# SmoothIR

<p align="center">
    <img src="https://github.com/brummer10/SmoothIR/blob/main/SmoothIR.png?raw=true" />
</p>

**SmoothIR** is a tool for creating impulse responses (IRs) through spectral matching of two audio files  
– with a focus on **musically useful results**, stability, and predictable behavior.

The idea is simple:

> The spectral difference between a *Reference* and a *Source* is transformed into an IR  
> – then carefully shaped using EQ-style controls.

The result is an IR that behaves like a **well-tuned filter**, ideal for creative applications such as guitars, reamping, and sound design.

---

## Features

* Spectral matching (Reference vs. Source)
* Robust handling of missing inputs (Reference-only / Source-only modes)
* Minimum-phase IR generation with adjustable IR length
* **EQ-style low/high rolloff (true dB/oct behavior)**
* Continuous **Smooth** control for musical shaping
* Stable, deterministic processing (no heuristics, no randomness)
* Interactive spectrum UI with band-style workflow

---

## IR Length

The **IR length** defines the duration of the generated impulse response and directly affects frequency resolution and low-frequency accuracy.

* **Longer IRs** → better low-end resolution and smoother matching  
* **Shorter IRs** → lower latency and CPU usage  

Rule of thumb:

    f_min ≈ sampleRate / IR_length

Practical ranges:

* 2048 samples → ~40–50 Hz  
* 4096 samples → ~25–30 Hz  
* 8192+ samples → recommended for deep low-end work  

Frequencies below ~20 Hz are automatically de-emphasized to avoid instability and unnecessary energy.

---

## Usage

    smoothir -r <reference.wav> -s <source.wav>

### Parameters

* `-r`, `--ref`  
  Reference file (target sound)

* `-s`, `--src`  
  Source file (input sound)

---

## Sound Shaping

After spectral matching, the IR is shaped in a controlled and predictable way.

### Low / High Cut (EQ-style)

SmoothIR uses a **true EQ-like rolloff**:

* Defined in **dB per octave**
* Monotonic and stable (no low-end rebound, no ripple)
* Comparable to analog-style highpass/lowpass filters (Butterworth-like)

This ensures:

* Clean low-end (no rumble buildup)  
* Smooth high-end (no harsh fizz)  
* No unexpected spectral artifacts  

---

### Smooth

A continuous control for spectral smoothing:

* `0.0` → maximum detail transfer  
* `~0.2 – 0.4` → musical sweet spot  
* `1.0` → heavily smoothed  

Internally:

> The raw transfer function is blended with a smoothed version  
> → preserving tone while removing narrow resonances and noise  

---

## Analysis Modes

SmoothIR behaves robustly depending on available inputs:

* **Reference + Source** → full spectral matching  
* **Reference only** → spectral capture  
* **Source only** → spectrum capture  

Missing inputs do **not** produce undefined behavior.

---

## UI / Interaction (EQ-style Workflow)

SmoothIR follows a modern parametric EQ interaction model:

* **Drag & Drop Bands**
  * Up / Down → Gain  
  * Left / Right → Frequency  

* **Mouse Wheel**
  * Adjusts **Q (bandwidth)**  

* Immediate visual feedback via spectrum display  
* Band-oriented workflow similar to professional EQs  

Goal:

> Make IR creation feel like working with an EQ – not a black box

---

## Typical Workflow

1. Load Reference and Source  
2. Generate IR  
3. Set Low/High Cut (define usable range)  
4. Adjust bands (freq / gain / Q)  
5. Dial in Smooth  
6. Apply IR  

---

## Notes

* Extreme low/high frequencies often contain little usable information  
  → controlled rolloff improves stability and sound quality  

* Over-smoothing removes character  
  → use moderately  

* The generated IR is **minimum-phase**  
  → efficient, low latency, and practical  

---

## Example

A creative use case:

* Reference: Piano  
* Source: Harp  
* Applied to: Guitar  

→ produces a unique but still musical tonal character

---

## Dependencies

SmoothIR relies on a small set of widely available libraries:

* **X11** – windowing (Linux)  
* **cairo** – UI rendering  
* **libsndfile** – audio I/O  
* **FFTW3** – spectral processing  
* **jackd** – real-time audio  

### Install (Debian/Ubuntu)

    sudo apt install libx11-dev libcairo2-dev libsndfile1-dev libfftw3-dev libjack-jackd2-dev

---

## Build

    git clone https://github.com/brummer10/SmoothIR.git
    cd SmoothIR
    git submodule init
    git submodule update
    make
    sudo make install

---

## Concept

SmoothIR deliberately avoids:

* heuristic corrections  
* unstable spectral tricks  
* artificial post-processing  

Instead:

> Clear separation of **analysis → shaping → synthesis**

Resulting in predictable, reproducible, and musical behavior.

---

## License

BSD-3-Clause

---

## Philosophy

> If it sounds good, it is right.
