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
#include "IrMatch.h"
#include "SpectrumViewer.h"

int main(int argc, char *argv[]){

    CmdParser cmd;
    AudioFile af;
    IRProcessor ip;
    SpectrumViewer sw(&ip);
    std::vector<double> srcf;
    std::vector<double> dstf;

    if (!cmd.parseCmdLine(argc, argv)) {
        cmd.printUsage(argv[0]);
        return 1;
    }

    std::string src = cmd.opt.src.value_or("");
    std::string dst = cmd.opt.dst.value_or("");

    if(!src.empty()) {
        if (! af.getAudioFile(src.c_str(), 48000) ) return 1;
        for (uint32_t i = 0; i < af.samplesize; i++) {
            srcf.push_back((double)af.samples[i]);
        }
        sw.setSource(srcf,src);
    }
    if(!dst.empty()) {
        if (! af.getAudioFile(dst.c_str(), 48000) ) return 1;
        for (uint32_t i = 0; i < af.samplesize; i++) {
            dstf.push_back((double)af.samples[i]);
        }
        sw.setRef(dstf,dst);
    }

    ip.computeIR(dstf, srcf, 48000, 2048);
    sw.setData(ip.getRefMag(), ip.getSrcMag(), ip.getDiffMag(), ip.getIRMag());
    sw.show();

    printf("bye bye\n");
    return 0;
}

