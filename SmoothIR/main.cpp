/*
 * main.c
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2025 brummer <brummer@web.de>
 */


#include <cmath>
#include <vector>
#include <signal.h>
#include <cstdio>
#include <algorithm>
#include <unistd.h>
#include <iostream>
#include <string>
#include <condition_variable>

#include "CmdParser.h"
#include "AudioFile.h"
#include "FFTAnalyzer.h"
#include "IrMatch.h"
#include "IrMorpher.h"
#include "Gain.h"


#include "SpectrumViewer.h"

#include "JackClient.h"


int main(int argc, char *argv[]){

    CmdParser cmd;
    AudioFile af;
    FFTAnalyzer ana;
    IRProcessor ip;
    IRMorpher conv;
    Gain vu;
    SpectrumViewer sw(&ip, &conv, &ana, &vu);
    JackClient jack(&conv, &sw, &ana, &vu);

    std::vector<double> srcf;
    std::vector<double> dstf;
    bool run = false;

    if (!cmd.parseCmdLine(argc, argv)) {
        cmd.printUsage(argv[0]);
        return 1;
    }

    std::string src = cmd.opt.src.value_or("");
    std::string dst = cmd.opt.dst.value_or("");
    std::string ir_file = cmd.opt.ir.value_or("");
    int sr = cmd.opt.sampleRate.value_or(48000);

    if(ir_file.empty()) {
        run = jack.start();
        sr = sw.getSampleRate();
    }

    if(!src.empty()) {
        if (! af.getAudioFile(src.c_str(), sr) ) return 1;
        for (uint32_t i = 0; i < af.samplesize; i++) {
            srcf.push_back((double)af.samples[i]);
        }
        sw.setSource(srcf,src);
    }
    if(!dst.empty()) {
        if (! af.getAudioFile(dst.c_str(), sr) ) return 1;
        for (uint32_t i = 0; i < af.samplesize; i++) {
            dstf.push_back((double)af.samples[i]);
        }
        sw.setRef(dstf,dst);
    }

    ip.computeIR(dstf, srcf, sr, 4096, true);

    if(!ir_file.empty()) {
        while (!ip.workerReady) 
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        std::vector<double> ir = ip.createIR();
        af.saveAudioFile(ir_file, ir, ir.size(), sr);
        std::cout << "save as: " << ir_file << std::endl;
    } else if (run) {
        sw.show();
        jack.stop();
    }

    printf("bye bye\n");
    return 0;
}

