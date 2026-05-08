
/*
 * IrMatch.h
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */

#pragma once

#include <algorithm>
#include <vector>
#include <complex>
#include <cmath>
#include <thread>
#include <atomic>
#include <fftw3.h>

struct Band {
    enum Type {LowShelf = 0,  Peak = 1, HighShelf = 2 };

    int enabled;
    Type type;
    double freq;
    double gain;
    double Q;
    int mute;
};

class IRProcessor {
public:
    using Complex = std::complex<double>;
    using CVec = std::vector<Complex>;
    using Vec  = std::vector<double>;

    std::atomic<bool> workerReady {false};
    std::atomic<bool> workerBusy {false};

    ~IRProcessor() {
        stopWorker();
    }

    IRProcessor() {
        front.store(&bufferA);
        back = &bufferB;
        startWorker();
    }

    struct alignas(64) IRData {
        Vec ref;
        Vec diff;
        Vec src;
        double peak = 0.0;
    };

    Band bands[6] = {
        // Low Shelf
        {0, Band::LowShelf,  80.0,   0.0, 0.7, 0},
        // Low-Mid
        {0, Band::Peak,     150.0,   0.0, 1.0, 0},
        // Mid 1
        {0, Band::Peak,     500.0,   0.0, 1.0, 0},
        // Mid 2
        {0, Band::Peak,    1500.0,   0.0, 1.0, 0},
        // High-Mid
        {0, Band::Peak,    4500.0,   0.0, 1.0, 0},
        // High Shelf
        {0, Band::HighShelf, 10000.0, 0.0, 0.7, 0}
    };

    void computeIR(const Vec& reference, const Vec& source, double sampleRate_,
                   size_t irLength_ = 4096, bool rebuild = false, size_t fftSize = 0) {
        sampleRate = sampleRate_;
        irLength = irLength_;
        if (source.size()) {
            haveSource = true;
        } else {
            haveSource = false;
        }
        if (reference.size()) {
            haveReference = true;
        } else {
            haveReference = false;
        }
        size_t maxAnalysisSize = sampleRate * 4;
        
        Vec ref_trunc = center_crop(reference, maxAnalysisSize);
        Vec src_trunc = center_crop(source, maxAnalysisSize);
        
        analysisN = (fftSize > 0) ? fftSize : next_pow2(std::max<size_t>(ref_trunc.size(), src_trunc.size()));
        analysisN = std::max<size_t>(analysisN, irLength * 2);
        synthesisN = next_pow2(irLength * 2);
        updateIR(ref_trunc, src_trunc, rebuild);
    }

    Vec createIR() {
        Vec synthMag = remap_mag_bins(mag_ir_, analysisN, synthesisN);
        CVec Hs = spectrum2fft(synthMag);
        CVec Hmin = mps(Hs);
        CVec ir_full = ifft(Hmin);
        size_t tail = std::min<size_t>(64, irLength / 4);
        apply_window(ir_full, tail);
        Vec ir(irLength);
        for (size_t i = 0; i < irLength; ++i)
            ir[i] = ir_full[i].real();
        normalize(ir);
        return ir;
    }

    Vec buildIR(const Vec& mag) {
        CVec Hs = spectrum2fft(mag);
        CVec Hmin = mps(Hs);
        CVec ir_full = ifft(Hmin);
        size_t tail = std::min<size_t>(64, irLength / 4);
        apply_window(ir_full, tail);
        Vec ir(irLength);
        for (size_t i = 0; i < irLength; ++i)
            ir[i] = ir_full[i].real();
        normalize(ir);
        return ir;
    }

    const Vec& getIRMag() const { return mag_ir_; }

    const Vec& getDiffMag() const {
        return front.load(std::memory_order_acquire)->diff;
    }

    const Vec& getRefMag() const {
        return front.load(std::memory_order_acquire)->ref;
    }

    const Vec& getSrcMag() const {
        return front.load(std::memory_order_acquire)->src;
    }

    void setLowCut(double lc) { lowcut = lc; }
    void setLowCutEnabled(int lc) { lowcut_enabled = lc; }
    void setHighCut(double hc) { highcut = hc; }
    void setHighCutEnabled(int hc) { highcut_enabled = hc; }
    void setSmooth(double sc) { smooth_amount = sc; }
    void setDynamics(double cc) { dynamics_amount = cc; }
    void setTilt(double tc) { tilt_amount = tc; }
    void setIrLength(size_t length) { irLength = length; }
    void setFtype(int i, int ft) { bands[i].type = (Band::Type)ft; }
    void setFreq(int i, double f) { bands[i].freq = f; }
    void setFq(int i, double q) { bands[i].Q = q; }
    void setFgain(int i, double g) { bands[i].gain = g; }
    void setFenable(int i, int e) { bands[i].enabled = e; }
    void setMuteBand(int i, int e) { bands[i].mute = e; }

    void setSoloBand(int i, int e) {
        solo_band = i;
        solo_enabled = e;
    }

private:
    Vec mag_ir_;
    size_t analysisN = 0;
    size_t synthesisN = 0;
    static constexpr double EPS = 1e-12;
    static constexpr double lowEndCutoff = 150.0;
    size_t irLength = 4096;
    bool haveSource = false;
    bool haveReference = false;
    double peak = 0.0;
    double lowcut = 100.0;
    double highcut = 4000.0;
    double smooth_amount = 0.3;
    double dynamics_amount = 0.0;
    double tilt_amount = 0.0;
    double sampleRate = 48000.0;
    int lowcut_enabled = 0;
    int highcut_enabled = 0;
    int solo_band = 0;
    int solo_enabled = 0;

    IRData bufferA;
    IRData bufferB;
    std::atomic<IRData*> front { nullptr };
    IRData* back = nullptr;

    std::thread workerThread;
    std::atomic<bool> running { true };
    std::atomic<bool> hasWork { false };
    std::mutex workMutex;
    Vec pendingReference;
    Vec pendingSource;
    bool pendingRebuild = false;
    std::condition_variable cv;
    std::mutex cvMutex;

    static Vec remap_mag_bins(const Vec& in, size_t analysisN, size_t synthesisN) {
        size_t outBins = synthesisN / 2 + 1;
        size_t inBins  = in.size();
        Vec out(outBins);

        for (size_t i = 0; i < outBins; ++i) {
            double freqNorm = (double)i / (double)(outBins - 1);
            double srcPos = freqNorm * (double)(inBins - 1);
            size_t idx0 = (size_t)srcPos;
            size_t idx1 = std::min(idx0 + 1, inBins - 1);
            double frac = srcPos - (double)idx0;
            out[i] = in[idx0] * (1.0 - frac) + in[idx1] * frac;
        }

        return out;
    }

    static Vec center_crop(const Vec& in, size_t size) {
        if (in.size() <= size)
            return in;

        size_t start = (in.size() - size) / 2;
        return Vec(in.begin() + start, in.begin() + start + size);
    }

    void make_flat(Vec& v, size_t bins, double db = 0.0) {
        v.clear();
        v.assign(bins, db);
    }

    static inline double db_edge_fade(double db, double threshold, double width) {
        double t = (db - (threshold - width)) / (2.0 * width);
        t = std::clamp(t, 0.0, 1.0);
        return t * t * (3.0 - 2.0 * t);
    }

    static inline double edge_fade(double x, double width) {
        double t = (x + width) / (2.0 * width);
        t = std::clamp(t, 0.0, 1.0);
        return t * t * (3.0 - 2.0 * t);
    }

    Vec buildBandSoloIR(const Band& b, double sr) {
        size_t n = mag_ir_.size();
        Vec mag(n, -220.0); // alles erstmal tot

        const double nyquist = sr * 0.5;
        double edgeWidth = 0.02; 

        for (size_t i = 1; i < n; ++i) {

            double freq = (double)i / (n - 1) * nyquist;
            double x = log_distance(freq, b.freq);

            double db = 0.0;
            double mask = 0.0;

            switch (b.type) {

                case Band::Peak:
                {
                    db = eval_peak_db(freq, b.freq, b.gain, b.Q);
                    double adb = fabs(db);
                    const double threshold = 0.5;
                    const double edge = 0.5;
                    
                    if (adb > threshold + edge) {
                        mask = 1.0;
                    } else if (adb > threshold - edge) {
                        mask = db_edge_fade(adb, threshold, edge);
                    } else {
                        mask = 0.0;
                    }
                    db += 12.0;
                    break;
                }
                case Band::LowShelf:
                {
                    db = eval_low_shelf(freq, b.freq, b.gain, b.Q);
                    if (x < -edgeWidth) mask = 1.0;
                    else if (x < edgeWidth) mask = 1.0 - edge_fade(x, edgeWidth);
                    else mask = 0.0;
                    db += 12.0;
                    break;
                }
                case Band::HighShelf:
                {
                    db = eval_high_shelf(freq, b.freq, b.gain, b.Q);
                    if (x > edgeWidth) mask = 1.0;
                    else if (x > -edgeWidth) mask = edge_fade(x, edgeWidth);
                    else mask = 0.0;
                    db += 12.0;
                    break;
                }
            }
            mag[i] = db * mask + (-220.0) * (1.0 - mask);
        }

        mag[0] = mag[1];
        return mag;
    }

    Vec buildBandMuteIR(const Band& b, double sr) {
        size_t n = mag_ir_.size();
        Vec mag = mag_ir_;
        const double nyquist = sr * 0.5;
        const double threshold = 0.5;
        const double edge = 0.5;

        for (size_t i = 1; i < n; ++i) {
            double freq = (double)i / (n - 1) * nyquist;
            double x = log_distance(freq, b.freq);
            double db = 0.0;
            double remove_mask = 0.0;

            switch (b.type) {
                case Band::Peak:
                {
                    db = eval_peak_db(freq, b.freq, b.gain, b.Q);
                    double adb = fabs(db);

                    if (adb > threshold + edge)
                        remove_mask = 1.0;
                    else if (adb > threshold - edge)
                        remove_mask = db_edge_fade(adb, threshold, edge);
                    else
                        remove_mask = 0.0;
                    break;
                }
                case Band::LowShelf:
                {
                    if (x < -edge) remove_mask = 1.0;
                    else if (x < edge) remove_mask = 1.0 - db_edge_fade(x + edge, edge, edge);
                    else remove_mask = 0.0;
                    break;
                }
                case Band::HighShelf:
                {
                    if (x > edge) remove_mask = 1.0;
                    else if (x > -edge) remove_mask = db_edge_fade(x + edge, edge, edge);
                    else remove_mask = 0.0;
                    break;
                }
            }
            double keep = 1.0 - remove_mask;
            mag[i] = mag[i] * keep + (-220.0) * remove_mask;
        }
        mag[0] = mag[1];
        return mag;
    }

    void aplayFilter() {
        if (!mag_ir_.size()) return;

        double lowcut_ = lowcut;
        double highcut_ = highcut;
        double smooth_amount_ = smooth_amount;
        double dynamics_amount_ = dynamics_amount;
        double tilt_amount_ = tilt_amount;
        int lowcut_enabled_ = lowcut_enabled;
        int highcut_enabled_ = highcut_enabled;
        int solo_band_ = solo_band;
        int solo_enabled_ = solo_enabled;

        if(lowcut_enabled_) {
            apply_low_rolloff(mag_ir_, sampleRate, lowcut_);
        } else if (haveSource || haveReference) {
            apply_low_rolloff(mag_ir_, sampleRate, 30.0);
        }
        if(highcut_enabled_) apply_high_rolloff(mag_ir_, sampleRate, highcut_);

        Band localBands[6];
        std::copy(std::begin(bands), std::end(bands), std::begin(localBands));

        for (auto& b : localBands) {
            if (b.enabled) {
                switch (b.type) {
                    case Band::Peak:
                        apply_peak(mag_ir_, sampleRate, b.freq, b.gain, b.Q);
                        break;

                    case Band::LowShelf:
                        apply_low_shelf(mag_ir_, sampleRate, b.freq, b.gain, b.Q);
                        break;

                    case Band::HighShelf:
                        apply_high_shelf(mag_ir_, sampleRate, b.freq, b.gain, b.Q);
                        break;
                }
            }
        }
        
        //apply_peak(mag_ir_, sampleRate, 1000.0, -24.0, 1.0); // Q 0.0 - 5  
        Vec smooth = adaptive_log_smooth(mag_ir_, sampleRate);
        mag_ir_ = lerpv(mag_ir_, smooth, smooth_amount_);
        mag_ir_ = spectral_dynamics(mag_ir_, dynamics_amount_, tilt_amount_, sampleRate);
        //mag_ir_ = adaptive_log_smooth(mag_ir_, sampleRate * 0.001);
        //mag_ir_ = harmonic_refine(mag_ir_, sampleRate);
        //mag_ir_ = soften_peaks(mag_ir_, 0.2);

        if (!haveSource && ! haveReference) {
            peak = std::max(peak, *std::max_element(mag_ir_.begin(), mag_ir_.end()));
            for (auto& v : mag_ir_) v -= peak;
        }

        if (solo_enabled_) {
            if (localBands[solo_band_].enabled) {
                mag_ir_ = buildBandSoloIR(localBands[solo_band_], sampleRate);
                mag_ir_ = harmonic_refine(mag_ir_, sampleRate);
            }
        } else {
            for (auto& b : localBands) {
                if (b.enabled) {
                    if (b.mute ) {
                        mag_ir_ = buildBandMuteIR(b, sampleRate);
                        mag_ir_ = harmonic_refine(mag_ir_, sampleRate);
                    }
                }
            }
        }
        reconstruct_low_end(mag_ir_, sampleRate);
    }

    void processIR(const Vec& reference, const Vec& source, bool rebuild, IRData& out) {
        workerBusy = true;
        workerReady = false;

        if (!rebuild) {
            if (IRData* current = front.load(std::memory_order_acquire)) {
                out = *current;
            }
        }

        if (rebuild) {
            CVec a(analysisN), b(analysisN);

            for (size_t i = 0; i < reference.size(); ++i)
                a[i] = reference[i];

            for (size_t i = 0; i < source.size(); ++i)
                b[i] = source[i];

            CVec f1 = fft(a);
            CVec f2 = fft(b);

            out.peak = 0.0;

            if (haveSource && haveReference) {
                CVec H = safe_divide(f1, f2);
                out.diff = magnitude_db(H);
            } else if (haveReference) {
                out.diff = magnitude_db(f1);
            } else if (haveSource) {
                out.diff = magnitude_db(f2);
            } else {
                make_flat(out.ref, analysisN / 2 + 1);
                make_flat(out.diff, analysisN / 2 + 1);
                make_flat(out.src, analysisN / 2 + 1);
            }

            if (haveSource || haveReference) {
                out.diff = adaptive_log_smooth(out.diff, sampleRate);
                out.diff = soften_peaks(out.diff, 0.2);
                out.diff = harmonic_refine(out.diff, sampleRate);

                out.ref = magnitude_db(f1);
                out.ref = adaptive_log_smooth(out.ref, sampleRate);

                out.src = magnitude_db(f2);
                out.src = adaptive_log_smooth(out.src, sampleRate);

                out.peak = std::max(
                    *std::max_element(out.ref.begin(), out.ref.end()),
                    *std::max_element(out.src.begin(), out.src.end())
                );

                for (auto& v : out.ref)  v -= out.peak;
                for (auto& v : out.src)  v -= out.peak;
                //double peak_d = *std::max_element(out.diff.begin(), out.diff.end());
                //peak_d = peak_d < out.peak ? out.peak : peak_d;
                for (auto& v : out.diff) v -= out.peak;
            }
        }

        mag_ir_  = remap_mag_bins(out.diff, analysisN, synthesisN);
        peak = out.peak;

        aplayFilter();

        workerReady = true;
        workerBusy = false;
    }

    void stopWorker() {
        running.store(false);
        cv.notify_one();

        if (workerThread.joinable())
            workerThread.join();
    }

    void startWorker() {
        workerThread = std::thread([this]() {
            while (running.load(std::memory_order_acquire)) {
                {
                    std::unique_lock<std::mutex> lock(cvMutex);
                    cv.wait_for(lock, std::chrono::milliseconds(100), [this]() {
                        return hasWork.load(std::memory_order_acquire) ||
                               !running.load(std::memory_order_acquire);
                    });
                }

                if (!running.load()) break;
                if (!hasWork.exchange(false)) continue;

                Vec reference, source;
                bool rebuild;
                {
                    std::lock_guard<std::mutex> lock(workMutex);
                    reference = pendingReference;
                    source = pendingSource;
                    rebuild = pendingRebuild;
                }

                processIR(reference, source, rebuild, *back);

                IRData* oldFront = front.exchange(back, std::memory_order_acq_rel);
                back = oldFront ? oldFront : (back == &bufferA ? &bufferB : &bufferA);
            }
        });
    }

    void updateIR(const Vec& reference, const Vec& source, bool rebuild) {
        {
            std::lock_guard<std::mutex> lock(workMutex);
            pendingReference = reference;
            pendingSource = source;
            pendingRebuild = rebuild;
        }

        hasWork.store(true, std::memory_order_release);
        cv.notify_one();
    }

    static Vec lerpv(const Vec& a, const Vec& b, double t) {
        size_t n = a.size();
        Vec out(n);

        for (size_t i = 0; i < n; ++i)
            out[i] = a[i] * (1.0 - t) + b[i] * t;

        return out;
    }

    static size_t next_pow2(size_t x) {
        size_t n = 1;
        while (n < x) n <<= 1;
        return n;
    }

    static double db(double x) {
        return 20.0 * std::log10(std::max<double>(x, EPS));
    }

    static double db2lin(double x) {
        return std::pow(10.0, x / 20.0);
    }

    static Vec magnitude_db(const CVec& f) {
        size_t n = f.size() / 2 + 1;
        Vec out(n);

        for (size_t i = 0; i < n; ++i)
            out[i] = db(std::abs(f[i]));

        return out;
    }

    static CVec safe_divide(const CVec& a, const CVec& b) {
        size_t n = a.size();
        CVec out(n);

        for (size_t i = 0; i < n; ++i) {
            double denom = std::norm(b[i]) + EPS;
            out[i] = a[i] * std::conj(b[i]) / denom;
        }

        return out;
    }

    Vec harmonic_refine(const Vec& mag, double sr) {

        size_t n = mag.size();
        Vec out = mag;
        const double nyquist = sr * 0.5;

        for (size_t i = 2; i < n - 2; ++i) {

            double freq = (double)i / (n - 1) * nyquist;
            if (freq < 80.0) continue;
            // local neighborhood
            double m2 = mag[i - 2];
            double m1 = mag[i - 1];
            double m0 = mag[i];
            double p1 = mag[i + 1];
            double p2 = mag[i + 2];
            // detect harmonic ridge
            double local_max = std::max<double>({m2, m1, m0, p1, p2});
            double local_avg = (m2 + m1 + p1 + p2) * 0.25;
            double contrast = local_max - local_avg;
            // only act when there's structure
            if (contrast < 1.0) continue;
            // pull neighbors slightly toward structure
            double pull = 0.15;
            //if (freq > 4000.0) pull *= 0.5;
            if (m0 < local_max) {
                out[i] = m0 + (local_max - m0) * pull;
            }
        }

        return out;
    }

    static double getSmoothing(double freq) {
        double x = std::log10(freq + 1.0);
        double s = 0.25 + 0.15 * x + 0.08 * x * x;
        return std::min(s, 1.8);
    }

    static Vec build_prefix_sum(const Vec& mag) {
        Vec ps(mag.size() + 1, 0.0);
        for (size_t i = 0; i < mag.size(); ++i)
            ps[i + 1] = ps[i] + mag[i];
        return ps;
    }

    static double range_sum(const Vec& ps, size_t i1, size_t i2) {
        return ps[i2 + 1] - ps[i1];
    }

    static Vec adaptive_log_smooth(const Vec& mag, double sr) {
        size_t n = mag.size();
        Vec out(n);
        Vec ps = build_prefix_sum(mag);
        const double nyquist = sr * 0.5;
        const double scale = (n - 1) / nyquist;

        for (size_t i = 1; i < n; ++i) {
            double freq = (double)i / (n - 1) * nyquist;
            double oct = getSmoothing(freq);
            double half = oct * 0.5;
            double f1 = freq * std::exp2(-half);
            double f2 = freq * std::exp2(half);
            size_t i1 = std::max<size_t>(1, (size_t)(f1 * scale));
            size_t i2 = std::min<size_t>(n - 1, (size_t)(f2 * scale));
            double sum = range_sum(ps, i1, i2);
            double count = (double)(i2 - i1 + 1);
            out[i] = sum / count;
        }

        out[0] = mag[0];
        return out;
    }

    static bool is_peak(const Vec& mag, size_t i) {
        return mag[i] > mag[i - 1] && mag[i] > mag[i + 1];
    }

    static Vec soften_peaks(const Vec& mag, double amount) {
        Vec out = mag;

        for (size_t i = 1; i < mag.size() - 1; ++i) {
            if (!is_peak(mag, i)) continue;
            double local_avg = (mag[i - 1] + mag[i + 1]) * 0.5;
            double excess = mag[i] - local_avg;

            if (excess > 0.0)
                out[i] = mag[i] - excess * amount;
        }
        return out;
    }

    Vec spectral_dynamics(const Vec& mag, double amount, double tilt, double sr) {
        Vec smooth = adaptive_log_smooth(mag, sr);
        Vec out = mag;
        size_t n = mag.size();
        const double nyquist = sr * 0.5;
        const double max_boost = 12.0;

        for (size_t i = 0; i < mag.size(); ++i) {
            double d = mag[i] - smooth[i];
            double factor = std::pow(2.0, amount);
            double freq = (double)i / (n - 1) * nyquist;
            double norm = std::log(freq / 20.0) / std::log(nyquist / 20.0);
            norm = std::clamp(norm, 0.0, 1.0);
            double centered = (norm - 0.5) * 2.0;
            double weight = 1.0 + tilt * std::tanh(centered);
            if (freq < 40.0) weight *= 0.5;
            weight = std::clamp(weight, 0.0, 2.0);
            double delta = d * factor * weight;
            delta = std::tanh(delta / max_boost) * max_boost;
            out[i] = smooth[i] + delta;
        }
        // dc block
        if (n > 1) out[0] = out[1];
        return out;
    }

    static double mapQ(double q_ui) {
        // clamp UI range
        q_ui = std::clamp(q_ui, 0.1, 10.0);
        // log-space mapping
        double x = std::log(q_ui);
        // soften curve
        double shaped = std::tanh(x * 0.8);
        // back to linear
        double q = std::exp(shaped * 1.5);
        return q;
    }

    static double q_to_sigma(double q) {
        return 1.0 / (1.5 * q + 0.5);
    }

    static inline double log_distance(double f, double f0) {
        return std::log2((f + 1e-9) / (f0 + 1e-9));
    }

    static double eval_peak_db(double freq, double f0, double gain, double Q_ui) {
        double Q = mapQ(Q_ui);
        double sigma = q_to_sigma(Q);
        double x = std::log2((f0 + 1e-9) / (freq + 1e-9));
        double g = std::exp(-0.5 * (x * x) / (sigma * sigma));
        return gain * g;
    }

    static void apply_peak(Vec& mag, double sr,
                double freq, double gain_db, double Q_ui) {
        size_t n = mag.size();
        double nyquist = sr * 0.5;
        double Q = mapQ(Q_ui);
        double sigma = q_to_sigma(Q);

        for (size_t i = 1; i < n; ++i) {
            double f = (double)i / (n - 1) * nyquist;
            if (f < 10.0) continue;
            double x = std::log2((f + 1e-9) / (freq + 1e-9));
            double g = std::exp(-0.5 * (x * x) / (sigma * sigma));
            mag[i] += gain_db * g;
        }
    }

    static double eval_low_shelf(double freq, double f0, double gain, double Q) {
        double slope = mapQ(Q) * 1.5;
        double x = log_distance(freq, f0);

        double g = 0.5 * (1.0 - std::tanh(slope * x));
        return gain * g;
    }

    static void apply_low_shelf(Vec& mag, double sr,
                    double freq, double gain_db, double Q) {
        size_t n = mag.size();
        double nyquist = sr * 0.5;
        double slope = mapQ(Q) * 1.5;

        for (size_t i = 1; i < n; ++i) {
            double f = (double)i / (n - 1) * nyquist;
            if (f < 10.0) continue;
            double x = log_distance(f, freq);
            double g = 0.5 * (1.0 - std::tanh(slope * x));
            mag[i] += gain_db * g;
        }
    }

    static double eval_high_shelf(double freq, double f0, double gain, double Q) {
        double slope = mapQ(Q) * 1.5;
        double x = log_distance(freq, f0);

        double g = 0.5 * (1.0 + std::tanh(slope * x));
        return gain * g;
    }

    static void apply_high_shelf(Vec& mag, double sr,
                    double freq, double gain_db, double Q) {
        size_t n = mag.size();
        double nyquist = sr * 0.5;
        double slope = mapQ(Q) * 1.5;

        for (size_t i = 1; i < n; ++i) {
            double f = (double)i / (n - 1) * nyquist;
            if (f < 10.0) continue;
            double x = log_distance(f, freq);
            double g = 0.5 * (1.0 + std::tanh(slope * x));
            mag[i] += gain_db * g;
        }
    }

    static void apply_low_rolloff(Vec& mag, double sr, double cutoff, int order = 4) {
        size_t n = mag.size();
        double nyquist = sr * 0.5;
        size_t cut = (size_t)(cutoff / nyquist * (n - 1));
        cut = std::min(cut, n - 1);
        double anchor = mag[cut];
        double anchor_lin = db2lin(anchor);
        const double norm = std::sqrt(2.0);

        for (size_t i = 0; i <= cut; ++i) {
            double f = (double)i / (n - 1) * nyquist;
            if (f < 1.0) f = 1.0;
            double H = 1.0 / std::sqrt(1.0 + std::pow(cutoff / f, 2.0 * order));
            H *= norm;
            double out = anchor_lin * H;
            mag[i] = db(out);
        }
    }

    static void apply_high_rolloff(Vec& mag, double sr, double cutoff, int order = 4) {
        size_t n = mag.size();
        double nyquist = sr * 0.5;
        size_t cut = (size_t)(cutoff / nyquist * (n - 1));
        cut = std::min(cut, n - 1);
        double anchor = mag[cut];
        double anchor_lin = db2lin(anchor);
        const double norm = std::sqrt(2.0);

        for (size_t i = cut; i < n; ++i) {
            double f = (double)i / (n - 1) * nyquist;
            double H = 1.0 / std::sqrt(1.0 + std::pow(f / cutoff, 2.0 * order));
            H *= norm;
            double out = anchor_lin * H;
            mag[i] = db(out);
        }
    }

    static void build_log_points(const Vec& mag, Vec& xs, Vec& ys,
                                    double sr, int numPoints = 16) {
        size_t n = mag.size();
        double nyquist = sr * 0.5;
        double fMin = 20.0;
        double fMax = 150.0;
        xs.resize(numPoints);
        ys.resize(numPoints);

        for (int i = 0; i < numPoints; ++i) {
            double t = (double)i / (numPoints - 1);
            double f = fMin * std::pow(fMax / fMin, t);
            size_t idx = (size_t)((f / nyquist) * (n - 1));
            idx = std::clamp(idx, (size_t)1, n - 1);
            xs[i] = std::log(f);
            ys[i] = mag[idx];
        }
    }

    static void clamp_outliers(Vec& ys) {
        for (size_t i = 1; i < ys.size() - 1; ++i) {
            double lo = std::min(ys[i - 1], ys[i + 1]);
            double hi = std::max(ys[i - 1], ys[i + 1]);
            ys[i] = std::clamp(ys[i], lo, hi);
        }
    }

    static void smooth_points(Vec& ys) {
        Vec tmp = ys;
        for (size_t i = 1; i < ys.size() - 1; ++i) {
            ys[i] = 0.25 * tmp[i - 1] + 0.5 * tmp[i] + 0.25 * tmp[i + 1];
        }
    }

    static double interp_monotonic( const Vec& xs, const Vec& ys, double x) {
        size_t n = xs.size();

        for (size_t i = 1; i < n; ++i) {
            if (x <= xs[i]) {
                double x0 = xs[i - 1];
                double x1 = xs[i];
                double y0 = ys[i - 1];
                double y1 = ys[i];
                double t = (x - x0) / (x1 - x0 + 1e-12);
                double m = (y1 - y0);
                return y0 + t * m;
            }
        }
        return ys.back();
    }

    static void reconstruct_low_end(Vec& mag, double sr) {
        size_t n = mag.size();
        double nyquist = sr * 0.5;
        size_t end = (size_t)((150.0 / nyquist) * (n - 1));
        end = std::min(end, n - 1);
        Vec xs, ys;
        build_log_points(mag, xs, ys, sr, 16);
        clamp_outliers(ys);
        smooth_points(ys);

        for (size_t i = 1; i < end; ++i) {
            double f = (double)i / (n - 1) * nyquist;
            double x = std::log(f + 1.0);
            mag[i] = interp_monotonic(xs, ys, x);
        }
        mag[0] = mag[1];
    }

    static void smooth_low_end_log(Vec& mag, double sr) {
        size_t n = mag.size();
        size_t end = (size_t)((150.0 / (sr * 0.5)) * (n - 1));
        end = std::min(end, n - 1);
        Vec logMag = mag;
        for (size_t i = 2; i < end; ++i) {
            double a = logMag[i - 1];
            double b = logMag[i];
            double c = logMag[i + 1];
            double smoothed = (a + b + c) / 3.0;
            mag[i] = 0.7 * smoothed + 0.3 * b;
        }
        mag[0] = mag[1];
    }

    static CVec spectrum2fft(const Vec& mag) {
        size_t n = 2 * (mag.size() - 1);
        CVec out(n);

        for (size_t i = 0; i < mag.size(); ++i)
            out[i] = std::polar(db2lin(mag[i]), 0.0);

        for (size_t i = 1; i < mag.size() - 1; ++i)
            out[n - i] = std::conj(out[i]);

        return out;
    }

    static void apply_window(CVec& ir, size_t tail) {
        size_t n = ir.size();

        for (size_t i = 0; i < tail; ++i) {
            double w = 0.54 - 0.46 * std::cos(M_PI * i / tail);
            ir[n - tail + i] *= w;
        }
    }

    static void normalize(Vec& b) {
        double gain = 0.0;
        double peak = 0.0;
        // get normalization peak
        for (size_t i = 0; i < b.size(); i++) {
            peak = std::max<double>(peak, std::abs( b[i])) ;
        }
        // apply normalize factor and get gain factor
        if (peak != 0.0) {
            peak = 1.0/peak;
            for (size_t i = 0; i < b.size(); i++) {
               b[i] *= peak;
               double v = b[i] ;
               gain += v*v;
            }
        }
        // apply gain square root factor when needed
        if (gain != 0.0) {
            gain = 1.0 / gain;
            for (size_t i = 0; i < b.size(); i++) {
                b[i] *= gain;
            }
        }
    }

    // Cepstrum min-phase
    static void fold(CVec& r) {
        size_t n = r.size();
        size_t nt = n / 2;

        for (size_t i = 1; i < nt; ++i)
            r[i] += std::conj(r[n - i]);

        for (size_t i = nt + 1; i < n; ++i)
            r[i] = 0.0;
    }

    static CVec mps(const CVec& s) {
        CVec log_s(s.size());

        for (size_t i = 0; i < s.size(); ++i)
            log_s[i] = std::log(std::max<double>(std::abs(s[i]), EPS));

        IRProcessor tmp;
        CVec cp = tmp.ifft(log_s);
        fold(cp);
        CVec out = tmp.fft(cp);
        for (auto& v : out)
            v = std::exp(v);

        return out;
    }

    CVec fft(const CVec& in) const {
        int N = (int)in.size();

        fftw_complex *input = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * N);
        fftw_complex *output = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * N);

        for (int i = 0; i < N; ++i) {
            input[i][0] = in[i].real();
            input[i][1] = in[i].imag();
        }

        fftw_plan p = fftw_plan_dft_1d(N, input, output, FFTW_FORWARD, FFTW_ESTIMATE);
        fftw_execute(p);

        CVec out(N);
        for (int i = 0; i < N; ++i)
            out[i] = Complex(output[i][0], output[i][1]);

        fftw_destroy_plan(p);
        fftw_free(input);
        fftw_free(output);

        return out;
    }

    CVec ifft(const CVec& in) const {
        int N = (int)in.size();

        fftw_complex *input = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * N);
        fftw_complex *output = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * N);

        for (int i = 0; i < N; ++i) {
            input[i][0] = in[i].real();
            input[i][1] = in[i].imag();
        }

        fftw_plan p = fftw_plan_dft_1d(N, input, output, FFTW_BACKWARD, FFTW_ESTIMATE);
        fftw_execute(p);

        CVec out(N);
        for (int i = 0; i < N; ++i)
            out[i] = Complex(output[i][0] / N, output[i][1] / N);

        fftw_destroy_plan(p);
        fftw_free(input);
        fftw_free(output);

        return out;
    }
};
