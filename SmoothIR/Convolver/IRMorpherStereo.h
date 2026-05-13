/*
 * IRMorpherStereo.h
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */

#pragma once

#include <vector>
#include <memory>
#include <cmath>
#include <cstring>
#include <atomic>
#include <utility>

#include "HybridConvolverStereo.h"
#include "ParallelThread.h"

class IRMorpherStereo {
public:
    using Vec  = std::vector<double>;

    IRMorpherStereo(size_t blockSize = 256, size_t headSize  = 64) {

        convA = std::make_unique<HybridConvolverStereo>(blockSize, headSize);

        convB = std::make_unique<HybridConvolverStereo>(blockSize, headSize);

        B = blockSize;

        bufferAL.resize(8194);
        bufferAR.resize(8194);

        bufferBL.resize(8194);
        bufferBR.resize(8194);

        prerollBlocksTotal = 0;
        prerollCounter = 0;
        prerolling = false;
        xrworker.start();
        xrworker.set<IRMorpherStereo, &IRMorpherStereo::preroll>(this);
    }

    ~IRMorpherStereo() {xrworker.stop();}

    // load initial stereo IR
    void init(const std::pair<Vec, Vec> ir) {

        convA->setIR(ir.first, ir.second);

        IrReady.store(true, std::memory_order_release);
    }

    // change stereo IR
    void setIR(const std::pair<Vec, Vec> ir) {

        convB->setIR(ir.first, ir.second);

        prerollBlocksTotal = convB->getNumParts() * 2;

        prerollCounter = 0;
        prerolling = true;

        IrReady.store(true, std::memory_order_release);
    }

    void setBypass(int bp) {
        bypass = bp;
    }

    void preroll() {
        convB->process(bz, inL, inR, bufferBL.data(), bufferBR.data());
    }

    void process(uint32_t nframes, const float* inputL, const float* inputR,
                                            float* outputL, float* outputR) {

        if (!IrReady.load(std::memory_order_acquire) || bypass) {

            if (outputL != inputL)
                std::memcpy(outputL, inputL, nframes * sizeof(float));

            if (outputR != inputR)
                std::memcpy(outputR, inputR, nframes * sizeof(float));

            return;
        }

        // preroll next IR in parallel thread
        if (prerolling) {
            inL = inputL;
            inR = inputR;
            bz = nframes;
            xrworker.runProcess();
        }

        // process active IR
        convA->process(nframes, inputL, inputR, bufferAL.data(), bufferAR.data());

        // normal processing
        if (!prerolling) {
            std::memcpy(outputL, bufferAL.data(), sizeof(float) * nframes);
            std::memcpy(outputR, bufferAR.data(), sizeof(float) * nframes);
            return;
        }
        //convB->process(nframes, inputL, inputR, bufferBL.data(), bufferBR.data());
        xrworker.processWait();
        // output old IR since preroll is finished
        prerollCounter++;
        if (prerollCounter < prerollBlocksTotal) {
            std::memcpy(outputL, bufferAL.data(), sizeof(float) * nframes);
            std::memcpy(outputR, bufferAR.data(), sizeof(float) * nframes);
            return;
        }

        // crossfade old to new IR
        for (size_t i = 0; i < nframes; ++i) {

            if (i < FADE_SAMPLES) {
                float t = (float)i / (float)FADE_SAMPLES;
                float gA = std::cos(t * M_PI * 0.5);
                float gB = std::sin(t * M_PI * 0.5);
                outputL[i] = bufferAL[i] * gA + bufferBL[i] * gB;
                outputR[i] = bufferAR[i] * gA + bufferBR[i] * gB;
            } else {
                outputL[i] = bufferBL[i];
                outputR[i] = bufferBR[i];
            }
        }

        // activate new IR
        convA.swap(convB);
        prerolling = false;
    }

private:

    ParallelThread     xrworker;
    std::unique_ptr<HybridConvolverStereo> convA;
    std::unique_ptr<HybridConvolverStereo> convB;

    size_t B = 0;

    // current IR output
    std::vector<float> bufferAL;
    std::vector<float> bufferAR;

    // next IR output
    std::vector<float> bufferBL;
    std::vector<float> bufferBR;

    std::atomic<bool> IrReady{false};

    int bypass = 0;

    // preroll state
    const size_t FADE_SAMPLES = 32;

    bool prerolling = false;
    const float* inL = nullptr;
    const float* inR = nullptr;
    uint32_t bz = 0;

    size_t prerollBlocksTotal = 0;
    size_t prerollCounter = 0;
};
