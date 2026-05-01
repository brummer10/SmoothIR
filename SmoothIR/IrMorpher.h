
/*
 * IRMorpher.h
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */


#pragma once

#include <vector>
#include <memory>
#include <cmath>
#include <cstring>
#include <atomic>

#include "HybridConvolver.h"

class IRMorpher {
public:
    IRMorpher(size_t blockSize = 256,
              size_t headSize  = 64) {

        convA = std::make_unique<HybridConvolver>(blockSize, headSize);
        convB = std::make_unique<HybridConvolver>(blockSize, headSize);

        B = blockSize;

        bufferA.resize(B);
        bufferB.resize(B);

        prerollBlocksTotal = 0;
        prerollCounter = 0;
        prerolling = false;
    }

    // load impulse response file
    void init(const std::vector<double>& ir) {
        convA->setIR(ir);
        IrReady.store(true, std::memory_order_release);
    }

    // change impulse response file
    void setIR(const std::vector<double>& ir) {
        convB->setIR(ir);
        prerollBlocksTotal = convB->getNumParts() * 2;
        prerollCounter = 0;
        prerolling = true;
        IrReady.store(true, std::memory_order_release);
    }

    void setBypass(int bp) {
        bypass = bp;
    }

    void process(uint32_t nframes, const float* input, float* output) {
        if (!IrReady.load(std::memory_order_acquire) || bypass) {
            if(output != input)
                memcpy(output, input, nframes*sizeof(float));
            return;
        }

        convA->process(input, bufferA.data());

        if (!prerolling) {
            std::memcpy(output, bufferA.data(), sizeof(float) * B);
            return;
        }
        // preroll
        convB->process(input, bufferB.data());
        prerollCounter++;
        if (prerollCounter < prerollBlocksTotal) {
            std::memcpy(output, bufferA.data(), sizeof(float) * B);
            return;
        }

        // fade
        for (size_t i = 0; i < B; ++i) {
            if (i < FADE_SAMPLES) {
                float t = (float)i / (float)FADE_SAMPLES;
                float gA = std::cos(t * M_PI * 0.5);
                float gB = std::sin(t * M_PI * 0.5);
                output[i] = bufferA[i] * gA + bufferB[i] * gB;
            } else {
                output[i] = bufferB[i];
            }
        }

        // switch
        convA.swap(convB);
        prerolling = false;
    }

private:
    std::unique_ptr<HybridConvolver> convA;
    std::unique_ptr<HybridConvolver> convB;

    size_t B = 0;

    std::vector<float> bufferA;
    std::vector<float> bufferB;

    std::atomic<bool> IrReady {false};
    int bypass = 0;
    // preroll state
    const size_t FADE_SAMPLES = 32;
    bool prerolling = false;
    size_t prerollBlocksTotal = 0;
    size_t prerollCounter = 0;
};
