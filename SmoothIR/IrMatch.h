
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


class IRProcessor {
public:
    using Complex = std::complex<double>;
    using CVec = std::vector<Complex>;
    using Vec  = std::vector<double>;

    ~IRProcessor() {
        stopWorker();
    }

    std::atomic<bool> workerReady {false};
    std::atomic<bool> workerBusy {false};
    static constexpr double EPS = 1e-12;
    size_t irLength = 2048;

    void computeIR(const Vec& reference, const Vec& source, double sampleRate_,
                   size_t irLength_ = 2048, bool rebuild = false, size_t fftSize = 0) {
        sampleRate = sampleRate_;
        irLength = irLength_;
        n = (fftSize > 0) ? fftSize : next_pow2(std::max<size_t>(reference.size(), source.size()));
        n = std::max<size_t>(n, irLength * 2);
        updateIR(reference, source, rebuild);
    }

    void aplayFilter() {
        if (!last_diff_.size()) return;
        mag_ir_.assign(last_diff_.begin(), last_diff_.end());
        apply_low_rolloff(mag_ir_, sampleRate, lowcut);
        apply_high_rolloff(mag_ir_, sampleRate, highcut);
        Vec smooth = adaptive_log_smooth(mag_ir_, sampleRate);
        mag_ir_ = lerpv(mag_ir_, smooth, smooth_amount);
        mag_ir_ = spectral_dynamics(mag_ir_, dynamics_amount, tilt_amount, sampleRate);
        //mag_ir_ = adaptive_log_smooth(mag_ir_, sampleRate * 0.001);
        //mag_ir_ = harmonic_refine(mag_ir_, sampleRate);
        //mag_ir_ = soften_peaks(mag_ir_, 0.2);

        Vec ir_ = createIR();
        CVec c(n);
        for (size_t i = 0; i < ir_.size(); ++i)
            c[i] = ir_[i];
        CVec f3 = fft(c);
        
        mag_ir_.clear();
        mag_ir_ = magnitude_db(f3);

        peak = std::max(peak, *std::max_element(last_diff_.begin(), last_diff_.end()));
        for (auto& v : mag_ir_) v -= peak;
    }

    Vec createIR() {
        CVec Hs = spectrum2fft(mag_ir_);
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
    const Vec& getDiffMag() const { return last_diff_; }
    const Vec& getRefMag() const { return last_ref_; }
    const Vec& getSrcMag() const { return last_src_; }

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

    void setLowCut(double lc) {
        lowcut = lc;
    }

    void setHighCut(double hc) {
        highcut = hc;
    }

    void setSmooth(double sc) {
        smooth_amount = sc;
    }

    void setDynamics(double cc) {
        dynamics_amount = cc;
    }

    void setTilt(double tc) {
        tilt_amount = tc;
    }

    void setIrLength(size_t length) {
        irLength = length;
    }

private:
    Vec mag_ir_;
    Vec last_ref_;
    Vec last_src_;
    Vec last_diff_;
    size_t n = 0;
    double peak = 0.0;
    double lowcut = 100.0;
    double highcut = 4000.0;
    double smooth_amount = 0.3;
    double dynamics_amount = 0.0;
    double tilt_amount = 0.0;
    double sampleRate = 48000.0;

    std::thread workerThread;

    void stopWorker() {
        if (workerThread.joinable())
            workerThread.join();
    }

    void updateIR(const Vec& reference, const Vec& source, bool rebuild) {
        workerBusy = true;
        workerReady = false;

        if (workerThread.joinable())
            workerThread.join();
    
        workerThread = std::thread([this, reference, source, rebuild]() {
            if (rebuild) {
                CVec a(n), b(n);

                for (size_t i = 0; i < reference.size(); ++i)
                    a[i] = reference[i];

                for (size_t i = 0; i < source.size(); ++i)
                    b[i] = source[i];

                CVec f1 = fft(a);
                CVec f2 = fft(b);

                CVec H = safe_divide(f1, f2);

                last_diff_ = magnitude_db(H);
                last_diff_ = adaptive_log_smooth(last_diff_, sampleRate);
                last_diff_ = soften_peaks(last_diff_, 0.2);
                last_diff_ = harmonic_refine(last_diff_, sampleRate);

                last_ref_ = magnitude_db(f1);
                last_ref_ = adaptive_log_smooth(last_ref_, sampleRate);
                last_src_ = magnitude_db(f2);
                last_src_ = adaptive_log_smooth(last_src_, sampleRate);

                // normalise dB
                peak = 0.0; // reset
                peak = std::max(
                    *std::max_element(last_ref_.begin(), last_ref_.end()),
                    *std::max_element(last_src_.begin(), last_src_.end())
                );
                for (auto& v : last_ref_) v -= peak;
                for (auto& v : last_src_) v -= peak;
                peak =  std::max(peak, *std::max_element(last_diff_.begin(), last_diff_.end()));
                for (auto& v : last_diff_) v -= peak;
            }
            aplayFilter();

            workerReady = true;
            workerBusy = false;
        });
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
        if (mag.size() > 1) out[0] = out[1];
        return out;
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
            peak = 0.8/peak;
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
};
