/*
 * HybridConvolverStereo.h
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Real-time safe stereo hybrid convolver
 *
 * Features
 *  - Zero latency FIR head
 *  - Partitioned FFT tail
 *  - No shared mutable IR state
 *  - Atomic IR snapshot swap
 *  - Variable host buffer size safe
 *  - Fixed internal partition size
 *  - In-place safe
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */

#pragma once

#include <vector>
#include <complex>
#include <atomic>
#include <memory>
#include <cstring>
#include <fftw3.h>

class HybridConvolverStereo {
public:
    using Complex = std::complex<double>;

    HybridConvolverStereo(size_t blockSize = 256, size_t headSize  = 64)
        : B(blockSize), H(headSize), N(blockSize * 2) {

        allocateFFT(rt[0]);
        allocateFFT(rt[1]);
        fifoInputL.resize(B, 0.0f);
        fifoInputR.resize(B, 0.0f);
        fifoOutputL.resize(B, 0.0f);
        fifoOutputR.resize(B, 0.0f);
    }

    ~HybridConvolverStereo() {
        destroyFFT(rt[0]);
        destroyFFT(rt[1]);

        auto* ir = currentIR.load();
        delete ir;

        auto* pending = pendingIR.load();
        delete pending;
    }

    size_t getNumParts() const {
        auto* ir = currentIR.load(std::memory_order_acquire);
        if (!ir) return 0;
        return ir->getNumParts();
    }

    bool isReady() const {
        return currentIR.load(std::memory_order_acquire) != nullptr;
    }

    void setIR(const std::vector<double>& irL, const std::vector<double>& irR) {
        auto* ir = new IRData(B);

        buildChannelIR(ir->ch[0], irL);
        buildChannelIR(ir->ch[1], irR);

        IRData* old = pendingIR.exchange(ir, std::memory_order_acq_rel);

        delete old;
    }

    void process(uint32_t nframes, const float* inL, const float* inR,
                                            float* outL, float* outR) {
        trySwapIR();
        auto* ir = currentIR.load(std::memory_order_acquire);

        if (!ir) {
            if (outL != inL)
                std::memcpy(outL, inL, sizeof(float) * nframes);

            if (outR != inR)
                std::memcpy(outR, inR, sizeof(float) * nframes);

            return;
        }

        for (uint32_t i = 0; i < nframes; ++i) {

            fifoInputL[fifoWritePos] = inL[i];
            fifoInputR[fifoWritePos] = inR[i];

            outL[i] = fifoOutputL[fifoReadPos];
            outR[i] = fifoOutputR[fifoReadPos];

            fifoOutputL[fifoReadPos] = 0.0f;
            fifoOutputR[fifoReadPos] = 0.0f;

            ++fifoWritePos;
            ++fifoReadPos;

            if (fifoWritePos >= B) {
                processBlock(rt[0], ir->ch[0], fifoInputL.data(), fifoOutputL.data());
                processBlock(rt[1], ir->ch[1], fifoInputR.data(), fifoOutputR.data());
                fifoWritePos = 0;
            }

            if (fifoReadPos >= B)
                fifoReadPos = 0;
        }
    }

private:

    struct ChannelIR {
        size_t numParts = 0;
        std::vector<float> head;
        std::vector<std::vector<Complex>> Hparts;
        ChannelIR() = default;
    };

    struct IRData {
        ChannelIR ch[2];

        size_t getNumParts() const {
            return std::max(ch[0].numParts, ch[1].numParts);
        }

        explicit IRData(size_t /*B*/) {}
    };

    struct Channel {
        std::vector<float> delayLine;
        std::vector<float> overlap;
        std::vector<std::vector<Complex>> Xhistory;
        std::vector<Complex> fftIn;
        std::vector<Complex> fftOut;
        fftw_plan planFwd = nullptr;
        fftw_plan planInv = nullptr;
    };

    Channel rt[2];

    const size_t B;
    const size_t H;
    const size_t N;

    std::vector<float> fifoInputL;
    std::vector<float> fifoInputR;

    std::vector<float> fifoOutputL;
    std::vector<float> fifoOutputR;

    size_t fifoWritePos = 0;
    size_t fifoReadPos  = 0;

    std::atomic<IRData*> currentIR {nullptr};
    std::atomic<IRData*> pendingIR {nullptr};

private:

    void allocateFFT(Channel& c) {

        c.fftIn.resize(N);
        c.fftOut.resize(N);

        c.planFwd = fftw_plan_dft_1d((int)N,
                reinterpret_cast<fftw_complex*>(c.fftIn.data()),
                reinterpret_cast<fftw_complex*>(c.fftOut.data()),
                FFTW_FORWARD, FFTW_MEASURE);

        c.planInv = fftw_plan_dft_1d((int)N,
                reinterpret_cast<fftw_complex*>(c.fftIn.data()),
                reinterpret_cast<fftw_complex*>(c.fftOut.data()),
                FFTW_BACKWARD, FFTW_MEASURE);

        c.delayLine.assign(H, 0.0f);
        c.overlap.assign(B, 0.0f);
    }

    void destroyFFT(Channel& c) {

        if (c.planFwd)
            fftw_destroy_plan(c.planFwd);

        if (c.planInv)
            fftw_destroy_plan(c.planInv);
    }

    void buildChannelIR(ChannelIR& out, const std::vector<double>& ir) {
        out.head.assign(H, 0.0f);
        // minimal phase IR using only first part for EQ'ing
        const size_t irLen = std::min<size_t>(4096, ir.size());

        for (size_t i = 0; i < H && i < irLen; ++i)
            out.head[i] = (float)ir[i];

        const size_t tailLen = (irLen > H) ? (irLen - H) : 0;
        out.numParts = (tailLen + B - 1) / B;
        out.Hparts.assign( out.numParts, std::vector<Complex>(N));
        std::vector<Complex> tmpIn(N);
        std::vector<Complex> tmpOut(N);

        fftw_plan plan = fftw_plan_dft_1d((int)N,
                reinterpret_cast<fftw_complex*>(tmpIn.data()),
                reinterpret_cast<fftw_complex*>(tmpOut.data()),
                FFTW_FORWARD, FFTW_ESTIMATE);

        for (size_t p = 0; p < out.numParts; ++p) {
            std::fill(tmpIn.begin(), tmpIn.end(), Complex(0.0));

            for (size_t i = 0; i < B; ++i) {
                size_t idx = H + p * B + i;

                if (idx < irLen)
                    tmpIn[i] = ir[idx];
            }

            fftw_execute(plan);
            out.Hparts[p] = tmpOut;
        }
        fftw_destroy_plan(plan);
    }

    void trySwapIR() {
        IRData* p = pendingIR.exchange(nullptr, std::memory_order_acq_rel);
        if (!p) return;
        prepareRTState(*p);
        IRData* old = currentIR.exchange(p, std::memory_order_acq_rel);
        delete old;
    }

    void prepareRTState(const IRData& ir) {
        for (int c = 0; c < 2; ++c) {
            rt[c].Xhistory.assign(ir.ch[c].numParts, std::vector<Complex>(N));

            for (auto& v : rt[c].Xhistory)
                std::fill(v.begin(),v.end(), Complex(0.0));

            std::fill(rt[c].overlap.begin(), rt[c].overlap.end(), 0.0f);
            std::fill(rt[c].delayLine.begin(), rt[c].delayLine.end(), 0.0f);
        }
    }

    void processBlock(Channel& rtch, const ChannelIR& ir, const float* input, float* output) {

        for (size_t i = 0; i < B; ++i) {

            double acc = 0.0;

            for (size_t k = 0; k < H; ++k) {
                int idx = (int)i - (int)k;
                double x = (idx >= 0) ? input[idx] : rtch.delayLine[H + idx];
                acc += x * ir.head[k];
            }

            output[i] = (float)acc;
        }

        for (size_t i = 0; i < H; ++i)
            rtch.delayLine[i] = input[B - H + i];

        if (ir.numParts == 0) return;

        std::fill(rtch.fftIn.begin(), rtch.fftIn.end(), Complex(0.0));

        for (size_t i = 0; i < B; ++i)
            rtch.fftIn[i] = input[i];

        fftw_execute(rtch.planFwd);

        for (size_t i = ir.numParts - 1; i > 0; --i) {
            rtch.Xhistory[i] = rtch.Xhistory[i - 1];
        }

        rtch.Xhistory[0] = rtch.fftOut;

        std::fill(rtch.fftIn.begin(), rtch.fftIn.end(), Complex(0.0));

        for (size_t p = 0; p < ir.numParts; ++p) {
            const auto& X = rtch.Xhistory[p];
            const auto& H = ir.Hparts[p];

            for (size_t i = 0; i < N; ++i)
                rtch.fftIn[i] += X[i] * H[i];
        }

        fftw_execute(rtch.planInv);

        for (size_t i = 0; i < B; ++i) {
            output[i] += (float)(rtch.fftOut[i].real() / N + rtch.overlap[i]);
            rtch.overlap[i] = (float)( rtch.fftOut[i + B].real() / N);
        }
    }
};
