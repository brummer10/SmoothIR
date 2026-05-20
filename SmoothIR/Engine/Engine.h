/*
 * engine.h
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */


#pragma once

#include <vector>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <iostream>
#include <cstring>
#include <thread>
#include <unistd.h>

#include "AudioFile.h"
#include "ParallelThread.h"


class Engine
{
public:
    ParallelThread               xrworker;
    AudioFile                    af;
    IRProcessor*                 ip = nullptr;
    IRMorpherStereo*             conv = nullptr;
    FFTAnalyzer*                 ana = nullptr;
    GainStereo*                  vu = nullptr;
    uint32_t                     s_rate = 48000;
    size_t                       irLength = 4096;          
    std::string                  revfile;
    std::string                  srcfile;
    std::vector<double>*         srcL;
    std::vector<double>*         srcR;
    std::vector<double>*         dstL;
    std::vector<double>*         dstR;
    std::atomic<bool>            execute {false};
    std::atomic<bool>            workToDo {false};
    std::atomic<bool>            loadRevFile {false};
    std::atomic<bool>            loadSrcFile {false};
    std::atomic<bool>            processIR {false};
    std::atomic<bool>            rebuild {false};
    std::atomic<bool>            waitForIR {false};
    std::atomic<bool>            dataReady {false};
    std::atomic<bool>            convLoadIR {false};

    inline Engine(IRProcessor *ip_, IRMorpherStereo* conv_, FFTAnalyzer* ana_, GainStereo* vu_);

    inline ~Engine();

    inline void init(uint32_t rate, int32_t rt_prio_, int32_t rt_policy_);
    inline void do_work_mono();
    inline void process(uint32_t nframes, const float* input, const float* input1, float* output, float* output1);

private:
    ParallelThread               par;
    float*                       abuffer = nullptr;
    uint32_t                     frames = 0;

    inline void processBuffer();
    inline void feedAnanlyzer(uint32_t nframes, float* output, float* output1);
};

inline Engine::Engine(IRProcessor *ip_, IRMorpherStereo* conv_, FFTAnalyzer* ana_, GainStereo* vu_) :
    xrworker(),
    par() {
        ip = ip_;
        conv = conv_;
        ana = ana_;
        vu = vu_;
        abuffer = new float[8192];
        memset(abuffer, 0, 8192 * sizeof(float));

        xrworker.start();
        par.start();
};

inline Engine::~Engine(){
    ana->cleanup();
    xrworker.stop();
    par.stop();
    delete[] abuffer;

};

inline void Engine::init(uint32_t rate, int32_t rt_prio_, int32_t rt_policy_) {
    s_rate = rate;

    ana->init(4096, (float)rate);
    vu->init(rate);

    execute.store(false, std::memory_order_release);

    xrworker.setThreadName("Worker");
    xrworker.set<Engine, &Engine::do_work_mono>(this);
    //xrworker.runProcess();

    par.setThreadName("RT");
    par.setPriority(rt_prio_, rt_policy_);
    par.set<Engine, &Engine::processBuffer>(this);
};

void Engine::do_work_mono() {
    execute.store(true, std::memory_order_release);

    if (loadRevFile.load(std::memory_order_acquire)) {
        if (!revfile.empty()) af.getAudioFile(revfile, s_rate);
        loadRevFile.store(false, std::memory_order_release);
        (*dstL).assign(af.samplesL.begin(), af.samplesL.end());
        (*dstR).assign(af.samplesR.begin(), af.samplesR.end());
        processIR.store(true, std::memory_order_release);
        rebuild.store(true, std::memory_order_release);
    }

    if (loadSrcFile.load(std::memory_order_acquire)) {
        if (!srcfile.empty()) af.getAudioFile(srcfile, s_rate);
        loadSrcFile.store(false, std::memory_order_release);
        (*srcL).assign(af.samplesL.begin(), af.samplesL.end());
        (*srcR).assign(af.samplesR.begin(), af.samplesR.end());
        processIR.store(true, std::memory_order_release);
        rebuild.store(true, std::memory_order_release);
    }

    if (processIR.load(std::memory_order_acquire)) {
        ip->computeIR((*dstL),(*dstR), (*srcL),(*srcR), s_rate, irLength, 
                                rebuild.load(std::memory_order_acquire));
        processIR.store(false, std::memory_order_release);
        waitForIR.store(true, std::memory_order_release);
    }

    if (convLoadIR.load(std::memory_order_acquire)) {
        conv->setIR(ip->createIRStereo());
        convLoadIR.store(false, std::memory_order_release);
    }

    execute.store(false, std::memory_order_release);
}

inline void Engine::processBuffer() {
    if (!frames) return;
        ana->processBlock(abuffer, frames);
}


inline void Engine::feedAnanlyzer(uint32_t nframes, float* output, float* output1) {
    for (uint32_t i = 0; i < nframes; ++i) {
        const float l = std::fabs(output[i]);
        const float r = std::fabs(output1[i]);
        abuffer[i] = (l > r) ? output[i] : output1[i];
    }

    frames = nframes;
    par.runProcess();
}

inline void Engine::process(uint32_t nframes, const float* input,
                const float* input1, float* output, float* output1) {

    if(nframes<1) return;
    conv->process(nframes, input, input1, output, output1);
    vu->process(nframes, output, output1, output, output1);

    feedAnanlyzer(nframes, output, output1);
    if(workToDo.load(std::memory_order_acquire) && !execute.load(std::memory_order_acquire)) {
        workToDo.store(false, std::memory_order_release);
        xrworker.runProcess();
    }
    if(waitForIR.load(std::memory_order_acquire) && ip->workerReady.load(std::memory_order_acquire)) {
        dataReady.store(true, std::memory_order_release);
        if (!execute.load(std::memory_order_acquire)) {
            waitForIR.store(false, std::memory_order_release);
            convLoadIR.store(true, std::memory_order_release);
            xrworker.runProcess();
        }
    }
}
