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
#include <utility>

#include "CmdParser.h"
#include "AudioFile.h"
#include "FFTAnalyzer.h"
#include "IRProcessorStereo.h"
#include "IRMorpherStereo.h"
#include "GainStereo.h"
#include "Engine.h"


#include "SpectrumViewer.h"

#include "JackClient.h"


int main(int argc, char *argv[]){

    CmdParser cmd;
    AudioFile af;
    FFTAnalyzer ana;
    IRProcessor ip;
    IRMorpherStereo conv;
    GainStereo vu;
    Engine engine(&ip, &conv, &ana, &vu);
    SpectrumViewer sw(&engine);
    JackClient jack(&engine, &sw);

    std::vector<double> srcL;
    std::vector<double> srcR;
    std::vector<double> dstL;
    std::vector<double> dstR;
    bool startUi = false;

    if (!cmd.parseCmdLine(argc, argv)) {
        cmd.printUsage(argv[0]);
        return 1;
    }

    std::string src = cmd.opt.src.value_or("");
    std::string dst = cmd.opt.dst.value_or("");
    std::string ir_file = cmd.opt.ir.value_or("");
    int sr = cmd.opt.sampleRate.value_or(48000);

    if(ir_file.empty()) {
        startUi = jack.start();
        sr = sw.getSampleRate();
    }

    if(!src.empty()) {
        if (! af.getAudioFile(src.c_str(), sr) ) return 1;
        for (uint32_t i = 0; i < af.samplesize; i++) {
            srcL.push_back((double)af.samplesL[i]);
            srcR.push_back((double)af.samplesR[i]);
        }
        sw.setSource(srcL,srcR,src);
    }
    if(!dst.empty()) {
        if (! af.getAudioFile(dst.c_str(), sr) ) return 1;
        for (uint32_t i = 0; i < af.samplesize; i++) {
            dstL.push_back((double)af.samplesL[i]);
            dstR.push_back((double)af.samplesR[i]);
        }
        sw.setRef(dstL,dstR,dst);
    }

    ip.computeIR(dstL,dstR, srcL,srcR, sr, 4096, true);

    if(!ir_file.empty()) {
        while (!ip.workerReady) 
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        std::pair<std::vector<double>, std::vector<double> > ir = ip.createIRStereo();        
        af.saveAudioFile(ir_file, ir.first, ir.second, sr);
        std::cout << "save as: " << ir_file << std::endl;
    } else if (startUi) {
        sw.init();
        sw.create();
        sw.show();
        Atom WM_DELETE_WINDOW = os_register_wm_delete_window(sw.top);
        sw.run = true;
        while (sw.run) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            XEvent xev;
            if (XCheckTypedWindowEvent(sw.getMain()->dpy, sw.top->widget, ClientMessage, &xev)){
                if (xev.xclient.data.l[0] == (long int)WM_DELETE_WINDOW) {
                    sw.quitGui();
                }
            }

            sw.check_spec();
            os_run_embedded(sw.getMain());
            sw.check_irmatch();
        }

        main_quit(sw.getMain());
        jack.stop();
    }

    printf("bye bye\n");
    return 0;
}

