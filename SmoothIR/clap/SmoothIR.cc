/*
 * SmoothIR.cc
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */


#include <atomic>
#include <errno.h>
#include <unistd.h>
#include <signal.h>

#include <iostream>
#include <fstream>
#include <cstring>
#include <string>
#include <cmath>

#include <locale.h>

#include "AudioFile.h"
#include "FFTAnalyzer.h"
#include "IRProcessorStereo.h"
#include "IRMorpherStereo.h"
#include "GainStereo.h"
#include "Engine.h"
#include "ParallelThread.h"
#include "Parameter.h"
#define CLAPPLUG
#include "SpectrumViewer.h"

class SmoothIR 
{
public:
    Widget_t*               TopWin;
    Params*                 param;
    AudioFile               af;
    FFTAnalyzer             ana;
    IRProcessor             ip;
    IRMorpherStereo         conv;
    GainStereo              vu;
    Engine                  engine;
    SpectrumViewer          sw;

    SmoothIR() : param(), ana(), ip(), conv(), vu(), engine(&ip, &conv, &ana, &vu), sw(&engine) {
        title = "SmoothEQ";
        firstLoop = true;
        p = 0;
        param = &sw.param;
    }

    ~SmoothIR() {
        fetch.stop();
    }

    void startGui(Window window) {
        main_init(sw.getMain());
        #if defined(_WIN32)
        sw.top  = create_window(sw.getMain(), (HWND) window, 0, 0, 875, 520);
        sw.top->func.expose_callback = sw.draw_window;
        #else
        sw.top  = create_window(sw.getMain(), (Window) window, 0, 0, 875, 520);
        sw.top->func.expose_callback = sw.draw_window;
        #endif
        sw.top->flags |= HIDE_ON_DELETE;
        widget_set_title(sw.top, title.c_str());
        sw.create();
        fetch.startTimeout(16);
        fetch.set<SmoothIR, &SmoothIR::runGui>(this);
    }

    void startGui() {
        main_init(sw.getMain());
        sw.top  = create_window(sw.getMain(), os_get_root_window(sw.getMain(), IS_WINDOW), 0, 0, 875, 520);
        sw.top->func.expose_callback = sw.draw_window;
        sw.top->flags |= HIDE_ON_DELETE;
        sw.create();
        fetch.startTimeout(16);
        fetch.set<SmoothIR, &SmoothIR::runGui>(this);
    }

    void showGui() {
        //engine._notify_ui.store(true, std::memory_order_release);
        getEngineValues();
        sw.ref_file = ref_file;
        sw.src_file = src_file;
        widget_show_all(sw.top);
        firstLoop = true;
    }
    
    void setParent(Window window) {
        #if defined(_WIN32)
        SetParent(sw.top->widget, (HWND) window);
        #else
        XReparentWindow(sw.getMain()->dpy, sw.top->widget, (Window) window, 0, 0);
        #endif
        p = window;
    }

    void checkParentWindowSize(int width, int height) {
        #if defined (IS_VST2)
        if (!p) return;
        int host_width = 1;
        int host_height = 1;
        #if defined(_WIN32)
        RECT rect;
        if (GetClientRect((HWND) p, &rect)) {
            host_width  = rect.right - rect.left;
            host_height = rect.bottom - rect.top;
        }
        #else
        XWindowAttributes attrs;
        if (XGetWindowAttributes(sw.getMain()->dpy, p, &attrs)) {
            host_width  = attrs.width;
            host_height = attrs.height;
        }
        #endif
        if ((host_width != width && host_width != 1) ||
            (host_height != height && host_height != 1)) {
            os_resize_window(sw.getMain()->.dpy, sw.top, host_width, host_height);
        }
        #endif
    }

    void hideGui() {
        widget_hide(sw.top);
        firstLoop = false;
    }

    void quitGui() {
        ref_file = sw.ref_file;
        src_file = sw.src_file;

        cleanup();
        fetch.stop();
        sw.quitGui();
        main_quit(sw.getMain());
    }

    void runGui() {
        if (firstLoop) {
            checkParentWindowSize(sw.top->width, sw.top->height);
            firstLoop = false;
        }        
        sw.check_spec();
        os_run_embedded(sw.getMain());
        sw.check_irmatch();
    }

    Xputty *getMain() {
        return sw.getMain();
    }

    Engine *getEngine() {
        return &engine;
    }

    void initEngine(uint32_t rate, int32_t prio, int32_t policy) {
        if (!sw.havePreset.load((std::memory_order_acquire)))
            ip.computeIR(sw.dstL,sw.dstR, sw.srcL,sw.srcR, rate, 4096, true);
        engine.init(rate, prio, policy);
    }

    inline void process(uint32_t n_samples, float* input, float* input1, float* output, float* output1) {
        engine.process(n_samples, input, input1, output, output1);
    }

    void getLatency(uint32_t* latency) {
        (*latency) = 0;
    }

    void copyValuesToGui(Widget_t* wid, float value) {
        xevfunc store = wid->func.value_changed_callback;
        adj_set_value(wid->adj, value);
        wid->func.value_changed_callback = store;
    }

    void getEngineValues() {
        copyValuesToGui(sw.bp,         (float)conv.bypass);
        copyValuesToGui(sw.fenable[0], (float)ip.bands[0].enabled);
        copyValuesToGui(sw.ftype[0],   (float)ip.bands[0].type);
        copyValuesToGui(sw.mute[0],    (float)ip.bands[0].mute);
        copyValuesToGui(sw.freq[0],    (float)ip.bands[0].freq);
        copyValuesToGui(sw.fgain[0],   (float)ip.bands[0].gain);
        copyValuesToGui(sw.fq[0],      (float)ip.bands[0].Q);

        copyValuesToGui(sw.fenable[1], (float)ip.bands[1].enabled);
        copyValuesToGui(sw.ftype[1],   (float)ip.bands[1].type);
        copyValuesToGui(sw.mute[1],    (float)ip.bands[1].mute);
        copyValuesToGui(sw.freq[1],    (float)ip.bands[1].freq);
        copyValuesToGui(sw.fgain[1],   (float)ip.bands[1].gain);
        copyValuesToGui(sw.fq[1],      (float)ip.bands[1].Q);

        copyValuesToGui(sw.fenable[2], (float)ip.bands[2].enabled);
        copyValuesToGui(sw.ftype[2],   (float)ip.bands[2].type);
        copyValuesToGui(sw.mute[2],    (float)ip.bands[2].mute);
        copyValuesToGui(sw.freq[2],    (float)ip.bands[2].freq);
        copyValuesToGui(sw.fgain[2],   (float)ip.bands[2].gain);
        copyValuesToGui(sw.fq[2],      (float)ip.bands[2].Q);

        copyValuesToGui(sw.fenable[3], (float)ip.bands[3].enabled);
        copyValuesToGui(sw.ftype[3],   (float)ip.bands[3].type);
        copyValuesToGui(sw.mute[3],    (float)ip.bands[3].mute);
        copyValuesToGui(sw.freq[3],    (float)ip.bands[3].freq);
        copyValuesToGui(sw.fgain[3],   (float)ip.bands[3].gain);
        copyValuesToGui(sw.fq[3],      (float)ip.bands[3].Q);

        copyValuesToGui(sw.fenable[4], (float)ip.bands[4].enabled);
        copyValuesToGui(sw.ftype[4],   (float)ip.bands[4].type);
        copyValuesToGui(sw.mute[4],    (float)ip.bands[4].mute);
        copyValuesToGui(sw.freq[4],    (float)ip.bands[4].freq);
        copyValuesToGui(sw.fgain[4],   (float)ip.bands[4].gain);
        copyValuesToGui(sw.fq[4],      (float)ip.bands[4].Q);

        copyValuesToGui(sw.fenable[5], (float)ip.bands[5].enabled);
        copyValuesToGui(sw.ftype[5],   (float)ip.bands[5].type);
        copyValuesToGui(sw.mute[5],    (float)ip.bands[5].mute);
        copyValuesToGui(sw.freq[5],    (float)ip.bands[5].freq);
        copyValuesToGui(sw.fgain[5],   (float)ip.bands[5].gain);
        copyValuesToGui(sw.fq[5],      (float)ip.bands[5].Q);

        if (ip.solo_enabled) copyValuesToGui(sw.solo[ip.solo_band], 1.0);

        copyValuesToGui(sw.lcenable,   (float)ip.lowcut_enabled);
        copyValuesToGui(sw.lowcut,     (float)ip.lowcut);
        copyValuesToGui(sw.hcenable,   (float)ip.highcut_enabled);
        copyValuesToGui(sw.highcut,    (float)ip.highcut);

        copyValuesToGui(sw.smooth,     (float)ip.smooth_amount);
        copyValuesToGui(sw.dynamics,   (float)ip.dynamics_amount);
        copyValuesToGui(sw.tilt,       (float)ip.tilt_amount);

        copyValuesToGui(sw.vug,        (float)vu.gain);
    }


    float check_stod (const std::string& str) {
        char* point = localeconv()->decimal_point;
        if (std::string(".") != point) {
            std::string::size_type point_it = str.find(".");
            std::string temp_str = str;
            if (point_it != std::string::npos)
                temp_str.replace(point_it, point_it + 1, point);
            return std::stod(temp_str);
        } else return std::stod(str);
    }

    std::string remove_sub(std::string a, std::string b) {
        std::string::size_type fpos = a.find(b);
        if (fpos != std::string::npos )
            a.erase(a.begin() + fpos, a.begin() + fpos + b.length());
        return (a);
    }

    void readState(std::string _stream) {
        std::string stream = _stream;
        std::string line;
        std::string key;
        std::string value;
        std::size_t pos = _stream.find("|");
        while (pos != std::string::npos) {
            line = stream.substr(0, pos);
            std::istringstream buf(line);
            buf >> key;
            buf >> value;
            if (key.compare("[CONTROLS]") == 0) {
                for (int i = 0; i < param->getParamCount(); i++) {
                    param->setParam(i, check_stod(value));
                    param->setParamDirty(i, true);
                    buf >> value;
                    if (!buf) break;
                }
            } else if (key.compare("[REFERENCE]") == 0) {
                sw.ref_file = remove_sub(line, "[REFERENCE] ");
                ref_file = sw.ref_file;
                engine.revfile = sw.ref_file;
                engine.srcL = &sw.srcL;
                engine.srcR = &sw.srcR;
                engine.loadRevFile.store(true, std::memory_order_release);
                engine.workToDo.store(true, std::memory_order_release);
                sw.havePreset.store(true, std::memory_order_release);
            } else if (key.compare("[SOURCE]") == 0) {
                sw.src_file = remove_sub(line, "[SOURCE] ");
                src_file = sw.src_file;
                engine.dstL = &sw.dstL;
                engine.dstR = &sw.dstR;
                engine.srcfile = sw.src_file;
                engine.loadSrcFile.store(true, std::memory_order_release);
                engine.workToDo.store(true, std::memory_order_release);
                sw.havePreset.store(true, std::memory_order_release);
            }
            key.clear();
            value.clear();
            stream = stream.substr(pos+1);
            pos = stream.find("|");
            if (pos == std::string::npos) break;
        }
        param->controllerChanged.store(true, std::memory_order_release);
    }

    void saveState(std::string *state) {
        std::ostringstream buffer; 
        buffer << "[CONTROLS] ";
        for (int i = 0; i < param->getParamCount(); i++) {
            buffer << param->getParam(i) << " ";
        }
        buffer << "|";

        buffer << "[REFERENCE] " << sw.ref_file << "|";
        buffer << "[SOURCE] " << sw.src_file << "|";

        (*state) = buffer.str();
    }

    void cleanup() {
        // Xputty free all memory used
        // main_quit(sw.getMain());
    }

private:
    ParallelThread          fetch;
    Window                  p;
    std::string             title;
    bool                    firstLoop;
    std::string             ref_file;
    std::string             src_file;

};
