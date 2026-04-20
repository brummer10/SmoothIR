
# SmoothIR

<p align="center">
    <img src="https://github.com/brummer10/SmoothIR/blob/main/SmoothIR.png?raw=true" />
</p>


**SmoothIR** is a tool for creating impulse responses (IRs) through spectral matching of two audio files
 – with a focus on musically useful results rather than purely technical accuracy.

The idea is simple:

> The spectral difference between a *Reference* and a *Source* is transformed into an IR
 – then deliberately smoothed and band-limited.

The result is an IR that works very well for creative applications (e.g. guitars, reamping, sound design).

---

## Features

* Spectral matching (Reference vs. Source)
* Minimum-phase IR generation
* Controllable low-/high-end rolloff
* “Smooth” control for musical smoothing
* Stable, deterministic results (no heuristic surprises)

---

## Usage

smoothir -r <reference.wav> -s <source.wav>

### Parameters

* `-r`, `--ref`  
  Path to the reference file (target sound)

* `-s`, `--src`  
  Path to the source file (input sound)

---

## Sound Shaping

After matching, the IR is further shaped:

### Low / High Cut

* Removes unwanted spectral regions
* Prevents low-end mud and high-end fizz
* Uses clean, monotonic rolloff (Butterworth-like behavior)

---

### Smooth

A continuous control for spectral smoothing:

* `0.0` → maximum detail transfer  
* `~0.2 – 0.4` → musical sweet spot  
* `1.0` → heavily smoothed, very soft result  

Internally, the original spectrum is blended with a smoothed version.

---

## Typical Workflow

1. Choose Reference and Source
2. Generate IR
3. Set Low/High Cut
4. Adjust Smooth to taste
5. Apply IR to target signal

---

##  Notes

* Low and high frequency extremes often contain little usable information  
  → controlled rolloff significantly improves stability and sound quality

* Too much smoothing can remove important details  
  → use moderately

* The generated IR is **minimum-phase**  
  → efficient and practical for real-world audio use

---

##  Example

An interesting use case:

* Reference: Piano  
* Source: Harp  
* Application: Guitar  

→ results in an IR with an unusual but musical character

---

## Build

```bash
 git clone https://github.com/brummer10/SmoothIR.git
 cd SmoothIR
 git submodule init
 git submodule update
 make
 sudo make install
```

---

##  Concept

SmoothIR intentionally avoids:

* post-processing the IR
* heuristic “fixes”
* unstable spectral fitting tricks

Instead:

> Clear separation between analysis, shaping, and synthesis


---

##  License

BSD-3-Clause

---

## 

If it sounds good, it is right.
