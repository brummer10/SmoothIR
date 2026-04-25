
/*
 * HybridConvolver.h
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */

#pragma once

#include <vector>
#include <complex>
#include <fftw3.h>
#include <cstring>

class HybridConvolver {
public:
    using Complex = std::complex<double>;

    HybridConvolver(size_t blockSize = 256, size_t headSize = 64)
        : B(blockSize), H(headSize), N(blockSize * 2)
    {}

    size_t getNumParts() const { return numParts; }
    bool isActivated() const { return isInited; }

    void setIR(const std::vector<double>& ir) {
        size_t irLen = ir.size();

        // head (time domain FIR)
        head.assign(H, 0.0);
        for (size_t i = 0; i < H && i < irLen; ++i)
            head[i] = ir[i];

        // tail (fft partitions)
        size_t tailLen = (irLen > H) ? (irLen - H) : 0;
        numParts = std::max<size_t>(1, (tailLen + B - 1) / B);

        H_parts.resize(numParts);
        X_history.assign(numParts, std::vector<Complex>(N, 0.0));

        fft_in.resize(N);
        fft_out.resize(N);

        plan_fwd = fftw_plan_dft_1d(N,
            reinterpret_cast<fftw_complex*>(fft_in.data()),
            reinterpret_cast<fftw_complex*>(fft_out.data()),
            FFTW_FORWARD, FFTW_MEASURE);

        plan_inv = fftw_plan_dft_1d(N,
            reinterpret_cast<fftw_complex*>(fft_in.data()),
            reinterpret_cast<fftw_complex*>(fft_out.data()),
            FFTW_BACKWARD, FFTW_MEASURE);

        // Partition tail (offset = H)
        for (size_t p = 0; p < numParts; ++p) {
            std::fill(fft_in.begin(), fft_in.end(), 0.0);

            for (size_t i = 0; i < B; ++i) {
                size_t idx = H + p * B + i;
                if (idx < irLen)
                    fft_in[i] = ir[idx];
            }

            fftw_execute(plan_fwd);

            H_parts[p].resize(N);
            for (size_t i = 0; i < N; ++i)
                H_parts[p][i] = fft_out[i];
        }

        overlap.assign(B, 0.0);
        delayLine.assign(H, 0.0);
        isInited = true;
    }

    void process(const float* input, float* output) {
        if (numParts < 1) return;

        // head (Zero Latency FIR)
        for (size_t i = 0; i < B; ++i) {
            double acc = 0.0;

            for (size_t k = 0; k < H; ++k) {
                int idx = (int)i - (int)k;
                double x = (idx >= 0) ? input[idx] : delayLine[H + idx];
                acc += x * head[k];
            }

            output[i] = acc;
        }

        // update delay line
        for (size_t i = 0; i < H; ++i)
            delayLine[i] = input[B - H + i];

        // fft tail
        std::fill(fft_in.begin(), fft_in.end(), 0.0);

        for (size_t i = 0; i < B; ++i)
            fft_in[i] = input[i];

        fftw_execute(plan_fwd);

        // shift history
        for (size_t i = numParts - 1; i > 0; --i)
            X_history[i] = X_history[i - 1];

        if (numParts > 0)
            std::copy(fft_out.begin(), fft_out.end(), X_history[0].begin());

        std::fill(fft_in.begin(), fft_in.end(), Complex(0.0, 0.0));

        for (size_t p = 0; p < numParts; ++p) {
            for (size_t i = 0; i < N; ++i) {
                fft_in[i] += X_history[p][i] * H_parts[p][i];
            }
        }

        fftw_execute(plan_inv);

        // sum (Head + Tail)
        for (size_t i = 0; i < B; ++i) {
            double tail = fft_out[i].real() / N + overlap[i];
            output[i] += tail;
        }

        for (size_t i = 0; i < B; ++i) {
            overlap[i] = fft_out[i + B].real() / N;
        }
    }

    ~HybridConvolver() {
        if (plan_fwd) fftw_destroy_plan(plan_fwd);
        if (plan_inv) fftw_destroy_plan(plan_inv);
    }

private:
    size_t B = 0;
    size_t H = 0;
    size_t N = 0;
    size_t numParts = 0;
    bool isInited = false;

    std::vector<double> head;
    std::vector<double> delayLine;

    std::vector<std::vector<Complex>> H_parts;
    std::vector<std::vector<Complex>> X_history;

    std::vector<Complex> fft_in;
    std::vector<Complex> fft_out;

    std::vector<double> overlap;

    fftw_plan plan_fwd = nullptr;
    fftw_plan plan_inv = nullptr;
};
