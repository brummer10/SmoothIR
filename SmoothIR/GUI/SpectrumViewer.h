
/*
 * SpectrumViewer.h
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */

#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <atomic>
#include <utility>

#include "Parameter.h"
#include "AudioFile.h"
#include "xwidgets.h"
#include "widgets.cc"
#include "EQController.h"

class SpectrumViewer {
public:
    using Vec = std::vector<float>;
    Params param;
    std::vector<double> dstL;
    std::vector<double> dstR;
    std::vector<double> srcL;
    std::vector<double> srcR;
    std::string ref_file;
    std::string src_file;
    bool run = false;
    Widget_t* top = nullptr;
    Widget_t* bp = nullptr;
    Widget_t* frame[6];
    Widget_t* ftype[6];
    Widget_t* fenable[6];
    Widget_t* freq[6];
    Widget_t* fq[6];
    Widget_t* fgain[6];
    Widget_t* solo[6];
    Widget_t* mute[6];

    Widget_t* lowcut = nullptr;
    Widget_t* lcenable = nullptr;
    Widget_t* highcut = nullptr;
    Widget_t* hcenable = nullptr;

    Widget_t* smooth = nullptr;
    Widget_t* dynamics = nullptr;
    Widget_t* tilt = nullptr;
    Widget_t* vug = nullptr;
    std::atomic<bool> havePreset {false};

    SpectrumViewer(Engine *engine_) {
        engine = engine_;
        registerParameters();
        engine->srcL = &srcL;
        engine->srcR = &srcR;
        engine->dstL = &dstL;
        engine->dstR = &dstR;
    }

    ~SpectrumViewer() {
        if(image_layer) cairo_surface_destroy(image_layer);
        if(eq_layer) cairo_surface_destroy(eq_layer);
    }

    void setRef(const std::vector<double>& refL, const std::vector<double>& refR, std::string ref_file_) {
        dstL.assign(refL.begin(), refL.end());
        dstR.assign(refR.begin(), refR.end());
        ref_file = ref_file_;
    }

    void setSource(const std::vector<double>& sourceL, const std::vector<double>& sourceR, std::string src_file_) {
        srcL.assign(sourceL.begin(), sourceL.end());
        srcR.assign(sourceR.begin(), sourceR.end());
        src_file = src_file_;
    }

    void setData(const std::vector<double>& ref,
                 const std::vector<double>& source,
                 const std::vector<double>& diff,
                 const std::vector<double>& ir) {
        ref_.assign(ref.begin(), ref.end());
        source_.assign(source.begin(), source.end());
        diff_.assign(diff.begin(), diff.end());
        ir_.assign(ir.begin(), ir.end());
    }

    void init(int width = 875, int height = 520) {
        main_init(&main);
        top = create_window(&main, os_get_root_window(&main, IS_WINDOW), 0, 0, width, height);
        widget_set_title(top, "Smoothed IR");
        widget_set_icon_from_png(top,LDVAR(smoothir_png));
        //top->flags = NO_PROPAGATE;
        top->func.expose_callback = draw_window;
    }

    void create(int width = 875, int height = 520) {
        spec_width  = 0;
        spec_height = 0;

        spec = create_widget(&main, top,0, 0, width-75, height-190);
        XSelectInput(spec->app->dpy, spec->widget,StructureNotifyMask|ExposureMask|KeyPressMask 
                    |EnterWindowMask|LeaveWindowMask|ButtonReleaseMask|KeyReleaseMask
                    |ButtonPressMask|Button1MotionMask|PointerMotionMask);

        spec->parent_struct = this;
        spec->func.expose_callback = draw_callback;
        spec->func.motion_callback = mouse_in_spec;
        spec->func.leave_callback = mouse_leave_spec;
        spec->func.button_release_callback = mouse_move_spec;
        spec->func.button_press_callback = mouse_set_spec;

        Widget_t* gframe = add_my_frame(top,"", width-73, 0, 71, height-192);
        vumeterL = add_my_vmeter(gframe, "Meter", false, 28, 5, 10, height-200);
        vumeterR = add_my_vmeter(gframe, "Meter", true, 38, 5, 10, height-200);
        vug = add_my_vslider(gframe, "Gain", 5, 5, 20, height-200);
        vug->parent_struct = this;
        set_adjustment(vug->adj,0.0, 0.0, -46.0, 12.0, 0.1, CL_CONTINUOS);
        vug->func.value_changed_callback = set_gain;

        Widget_t* lframe = add_my_frame(top,"", width-75, 331, 73, 100);
        curFreq = add_my_label(lframe, "",5,10,60,20);
        curGain = add_my_label(lframe, "",5,30,60,20);

        int x = 1;
        for (int i = 0; i<6; i++) {
            frame[i] = add_my_frame(top,"", x, 331, 133, 100);

            solo[i] = add_my_toggle_button(frame[i], 5, 5, 20, 20, "S");
            solo[i]->data = i;
            solo[i]->flags |= IS_RADIO;
            solo[i]->flags |= USE_TRANSPARENCY | FAST_REDRAW;
            solo[i]->parent_struct = this;
            solo[i]->func.value_changed_callback = solo_response;

            mute[i] = add_my_toggle_button(frame[i], 25, 5, 20, 20, "M");
            mute[i]->data = i;
            mute[i]->flags |= IS_RADIO;
            mute[i]->flags |= USE_TRANSPARENCY | FAST_REDRAW;
            mute[i]->parent_struct = this;
            mute[i]->func.value_changed_callback = mute_response;

            ftype[i] = add_type_combobox(frame[i], "Type", 50, 5, 55, 20);
            ftype[i]->data = i;
            ftype[i]->parent_struct = this;
            combobox_add_entry(ftype[i],"Low Shelf");
            combobox_add_entry(ftype[i],"Peak");
            combobox_add_entry(ftype[i],"High Shelf");
            if (i>0 && i<5) {
                combobox_set_active_entry(ftype[i], 1);
            } else if (i>=5) {
                combobox_set_active_entry(ftype[i], 2);
            } else {
                combobox_set_active_entry(ftype[i], 0);
            }
            ftype[i]->func.value_changed_callback = set_ftype;

            fenable[i] = add_my_enable_button(frame[i], 108, 5, 20, 20, "");
            fenable[i]->data = i;
            fenable[i]->parent_struct = this;
            fenable[i]->func.value_changed_callback = set_fenable;
            adj_set_value(fenable[i]->adj, engine->ip->bands[i].enabled);
            double r,g,bcol;
            get_band_color(i, r, g, bcol);

            set_widget_color(fenable[i], (Color_state)0, (Color_mod)0, r, g, bcol, 1.0);

            freq[i] = add_my_knob(frame[i], "FREQ", "Hz", 6,37,42, 60);
            freq[i]->data = i;
            freq[i]->parent_struct = this;
            freq[i]->func.value_changed_callback = set_freq;
            freq[i]->func.button_release_callback = set;
            if (i == 0) set_adjustment(freq[i]->adj, 80.0, 80.0, 20.0, 120.0, 0.01, CL_LOGARITHMIC);
            else if (i == 1) set_adjustment(freq[i]->adj, 150.0, 150.0, 80.0, 300.0, 0.01, CL_LOGARITHMIC);
            else if (i == 2) set_adjustment(freq[i]->adj, 500.0, 500.0, 250.0, 1000.0, 0.01, CL_LOGARITHMIC);
            else if (i == 3) set_adjustment(freq[i]->adj, 1500.0, 1500.0, 800.0, 3000.0, 0.01, CL_LOGARITHMIC);
            else if (i == 4) set_adjustment(freq[i]->adj, 4500.0, 4500.0, 2000.0, 8000.0, 0.01, CL_LOGARITHMIC);
            else if (i == 5) set_adjustment(freq[i]->adj, 10000.0, 10000.0, 6000.0, 20000.0, 0.01, CL_LOGARITHMIC);
            

            fq[i] = add_my_knob(frame[i], "Q", "", 48,37,42, 60);
            fq[i]->data = i;
            fq[i]->parent_struct = this;
            fq[i]->func.value_changed_callback = set_fq;
            fq[i]->func.button_release_callback = set;
            if (i == 0) set_adjustment(fq[i]->adj, 0.7, 0.7, 0.4, 10.0, 0.01, CL_LOGARITHMIC);
            else if (i == 1) set_adjustment(fq[i]->adj, 1.0, 1.0, 0.5, 10.0, 0.01, CL_LOGARITHMIC);
            else if (i == 2) set_adjustment(fq[i]->adj, 1.0, 1.0, 0.5, 10.0, 0.01, CL_LOGARITHMIC);
            else if (i == 3) set_adjustment(fq[i]->adj, 1.2, 1.2, 0.7, 10.0, 0.01, CL_LOGARITHMIC);
            else if (i == 4) set_adjustment(fq[i]->adj, 1.4, 1.4, 0.8, 10.0, 0.01, CL_LOGARITHMIC);
            else if (i == 5) set_adjustment(fq[i]->adj, 0.7, 0.7, 0.4, 10.0, 0.01, CL_LOGARITHMIC);

            fgain[i] = add_my_knob(frame[i], "GAIN", "dB", 90,37,42, 60);
            fgain[i]->data = i;
            fgain[i]->parent_struct = this;
            set_adjustment(fgain[i]->adj, 0.0, 0.0, -48.0, 24.0, 0.1, CL_CONTINUOS);
            fgain[i]->func.value_changed_callback = set_fgain;
            fgain[i]->func.button_release_callback = set;
            x += 133;
        }

        Widget_t* ref = add_my_file_button(top, 10, 435, 90, 20, "Reference:", " ", ".wav|.WAV");
        ref->flags |= USE_TRANSPARENCY | FAST_REDRAW;
        ref->parent_struct = this;
        ref->func.user_callback = ref_load_response;
        ref_label = add_my_label(top, "",100,437,265,20);
        ref_label->label = ref_file.data();

        Widget_t* remove_ref = add_my_button(top, 370, 435, 20, 20, "X");
        remove_ref->flags |= USE_TRANSPARENCY | FAST_REDRAW;
        remove_ref->parent_struct = this;
        remove_ref->func.value_changed_callback = remove_ref_response;

        Widget_t* src = add_my_file_button(top, 10, 465, 90, 20, "Source:", " ", ".wav|.WAV");
        src->flags |= USE_TRANSPARENCY | FAST_REDRAW;
        src->parent_struct = this;
        src->func.user_callback = src_load_response;
        src_label = add_my_label(top, "",100,467,265,20);
        src_label->label = src_file.data();

        Widget_t* remove_src = add_my_button(top, 370, 465, 20, 20, "X");
        remove_src->flags |= USE_TRANSPARENCY | FAST_REDRAW;
        remove_src->parent_struct = this;
        remove_src->func.value_changed_callback = remove_src_response;

        Widget_t* save = add_xsave_file_button(top, 10, 495, 90, 20, "Save IR", " ", ".wav|.WAV");
        save->flags = USE_TRANSPARENCY | FAST_REDRAW;
        save->parent_struct = this;
        save->func.user_callback = save_response;

        lowcut = add_my_knob(top, "LowCut", "Hz", 450,436,60, 80);
        float min_lc = get_min_lowcut(sampleRate, irLength);
        set_adjustment(lowcut->adj, 100.0, 100.0, min_lc, 2200.0, 0.01, CL_LOGARITHMIC);
        lowcut->flags |= USE_TRANSPARENCY | FAST_REDRAW;
        set_widget_color(lowcut, (Color_state)0, (Color_mod)0, 0.15, 0.52, 0.55, 1.0);
        lowcut->parent_struct = this;
        lowcut->func.value_changed_callback = set_lowcut;
        lowcut->func.button_release_callback = set;

        lcenable = add_my_enable_button(lowcut, 19, 15, 20, 20, "");
        lcenable->parent_struct = this;
        lcenable->func.value_changed_callback = set_lowcut_enable;

        highcut = add_my_knob(top, "HighCut", "Hz", 520,436,60, 80);
        set_adjustment(highcut->adj, 4000.0, 4000.0, 110.0, 22000.0, 0.01, CL_LOGARITHMIC);
        highcut->flags = USE_TRANSPARENCY | FAST_REDRAW;
        set_widget_color(highcut, (Color_state)0, (Color_mod)0, 0.15, 0.52, 0.55, 1.0);
        highcut->parent_struct = this;
        highcut->func.value_changed_callback = set_highcut;
        highcut->func.button_release_callback = set;

        hcenable = add_my_enable_button(highcut, 19, 15, 20, 20, "");
        hcenable->parent_struct = this;
        hcenable->func.value_changed_callback = set_highcut_enable;

        smooth = add_my_knob(top, "Smooth", "", 590,436,60, 80);
        set_adjustment(smooth->adj, 0.3, 0.3, 0.0, 1.0, 0.01, CL_CONTINUOS);
        smooth->flags |= USE_TRANSPARENCY | FAST_REDRAW;
        set_widget_color(smooth, (Color_state)0, (Color_mod)0, 0.15, 0.52, 0.55, 1.0);
        smooth->parent_struct = this;
        smooth->func.value_changed_callback = set_smooth;
        smooth->func.button_release_callback = set;

        dynamics = add_my_knob(top, "Dynamics", "", 660,436,60, 80);
        set_adjustment(dynamics->adj, 0.0, 0.0, -1.0, 1.0, 0.01, CL_CONTINUOS);
        dynamics->flags |= USE_TRANSPARENCY | FAST_REDRAW;
        set_widget_color(dynamics, (Color_state)0, (Color_mod)0, 0.15, 0.52, 0.55, 1.0);
        dynamics->parent_struct = this;
        dynamics->func.value_changed_callback = set_dynamics;
        dynamics->func.button_release_callback = set;

        tilt = add_my_knob(top, "Tone Bias", "", 730,436,60, 80);
        set_adjustment(tilt->adj, 0.0, 0.0, -1.0, 1.0, 0.01, CL_CONTINUOS);
        tilt->flags |= USE_TRANSPARENCY | FAST_REDRAW;
        set_widget_color(tilt, (Color_state)0, (Color_mod)0, 0.15, 0.52, 0.55, 1.0);
        tilt->parent_struct = this;
        tilt->func.value_changed_callback = set_tilt;
        tilt->func.button_release_callback = set;

        Widget_t* irsize = add_my_combobox(top, "Ir size", 805, 435, 60, 20);
        irsize->parent_struct = this;
        combobox_add_entry(irsize,"1024");
        combobox_add_entry(irsize,"2048");
        combobox_add_entry(irsize,"4096");
        combobox_add_entry(irsize,"8192");
        combobox_add_entry(irsize,"16384");
        combobox_set_active_entry(irsize, 2);
        irsize->func.value_changed_callback = set_irsize;
        add_tooltip(irsize->childlist->childs[0], "Ir length");

        bp = add_my_toggle_button(top, 805, 465, 60, 20, "Bypass");
        bp->flags |= USE_TRANSPARENCY | FAST_REDRAW;
        bp->parent_struct = this;
        bp->func.value_changed_callback = bp_response;

        #ifndef CLAPPLUG
        Widget_t* quit = add_my_button(top, 805, 495, 60, 20, "Quit");
        quit->flags |= USE_TRANSPARENCY | FAST_REDRAW;
        quit->parent_struct = this;
        quit->func.value_changed_callback = quit_response;
        #endif
    }

    void show() {
        widget_show_all(top);
    }

    void setSampleRate(const double sr) {
        sampleRate = sr;
    }

    const double getSampleRate() {
        return sampleRate;
    }

    void quitGui() {
        run = false;
        if (top) destroy_widget(top, top->app);
    }

    Xputty *getMain() {
        return &main;
    }

    void check_spec() {
        adj_set_value(vumeterL->adj, power2db(vumeterL, engine->vu->getMeterL()));
        adj_set_value(vumeterR->adj, power2db(vumeterR, engine->vu->getMeterR()));
        if (engine->ana->hasNewData()) {
            bin = engine->ana->getBins();
            mag_.clear();
            const float* m = engine->ana->getMagnitudes();
            for (int i = 0; i<bin; i++) {
                mag_.push_back(m[i]);
            }
            engine->ana->clearFlag();
            expose_widget(spec);
        }
    }

    void check_irmatch() {
        if (engine->dataReady.load(std::memory_order_acquire)) {
            engine->dataReady.store(false, std::memory_order_release);
            setData(engine->ip->getRefMag(), engine->ip->getSrcMag(), 
                            engine->ip->getDiffMag(), engine->ip->getIRMag());
            rebuild_layer = rebuild;
            rebuild_eq_layer = true;
            expose_widget(spec);
        }
    }

    static void draw_window(void* w_, void* user_data) {
        Widget_t* w = (Widget_t*)w_;
        cairo_t* cr = w->crb;
        cairo_set_source_rgb(cr, 0.157, 0.165, 0.212);
        cairo_paint(cr);
    }

private:
    Xputty main;
    Widget_t* ref_label = nullptr;
    Widget_t* src_label = nullptr;
    Widget_t* spec = nullptr;
    Widget_t* vumeterL = nullptr;
    Widget_t* vumeterR = nullptr;
    Widget_t* curFreq = nullptr;
    Widget_t* curGain = nullptr;
    Engine *engine = nullptr;
    AudioFile af;
    Vec ref_;
    Vec source_;
    Vec diff_;
    Vec ir_;
    Vec mag_;
    std::string ir_file;
    char cfreq[64];
    char cgain[64];
    size_t irLength = 2048;
    double sampleRate = 48000.0;
    bool band_match = false;
    int match_state = -1;
    int match_band = -1;
    int mx = 0;
    int my = 0;
    int bin = 0;
    int spec_width  = 0;
    int spec_height = 0;
    cairo_surface_t *image_layer = nullptr;
    bool rebuild_layer = true;
    cairo_surface_t *eq_layer = nullptr;
    bool rebuild_eq_layer = true;
    bool rebuild = true;
    std::atomic<bool> set_leak {false};

    const float f_min = 20.0f;
    const float f_max = 20000.0f;
    const float db_min = -96.0f;
    const float db_max = 24.0f;

    void registerParameters() {
        //                  name           group    min, max, def, step     value                               isStepped  type
        param.registerParam("Enable",     "Global",  0,   1,   0,    1,     (void*)&engine->conv->bypass,        true,  IS_INT);

        param.registerParam("Band 1 enable",  "EQ",  0,   1,   1,    1,     (void*)&engine->ip->bands[0].enabled,true,  IS_INT);
        param.registerParam("Band 1 type",    "EQ",  0,   2,   0,    1,     (void*)&engine->ip->bands[0].type,   true,  IS_INT);
        param.registerParam("Band 1 mute",    "EQ",  0,   1,   0,    1,     (void*)&engine->ip->bands[0].mute,   true,  IS_INT);
        param.registerParam("Band 1 freq",    "EQ", 20, 120,  80, 0.01,     (void*)&engine->ip->bands[0].freq,  false,  IS_DOUBLE);
        param.registerParam("Band 1 gain",    "EQ", -48, 24,   0, 0.01,     (void*)&engine->ip->bands[0].gain,  false,  IS_DOUBLE);
        param.registerParam("Band 1 Q",       "EQ", 0.4, 10, 0.7, 0.01,     (void*)&engine->ip->bands[0].Q,     false,  IS_DOUBLE);

        param.registerParam("Band 2 enable",  "EQ",  0,   1,   1,    1,     (void*)&engine->ip->bands[1].enabled,true,  IS_INT);
        param.registerParam("Band 2 type",    "EQ",  0,   2,   1,    1,     (void*)&engine->ip->bands[1].type,   true,  IS_INT);
        param.registerParam("Band 2 mute",    "EQ",  0,   1,   0,    1,     (void*)&engine->ip->bands[1].mute,   true,  IS_INT);
        param.registerParam("Band 2 freq",    "EQ", 80, 300, 150, 0.01,     (void*)&engine->ip->bands[1].freq,  false,  IS_DOUBLE);
        param.registerParam("Band 2 gain",    "EQ", -48, 24, 0.0, 0.01,     (void*)&engine->ip->bands[1].gain,  false,  IS_DOUBLE);
        param.registerParam("Band 2 Q",       "EQ", 0.5, 10, 1.0, 0.01,     (void*)&engine->ip->bands[1].Q,     false,  IS_DOUBLE);

        param.registerParam("Band 3 enable",  "EQ",  0,   1,   1,    1,     (void*)&engine->ip->bands[2].enabled,true,  IS_INT);
        param.registerParam("Band 3 type",    "EQ",  0,   2,   1,    1,     (void*)&engine->ip->bands[2].type,   true,  IS_INT);
        param.registerParam("Band 3 mute",    "EQ",  0,   1,   0,    1,     (void*)&engine->ip->bands[2].mute,   true,  IS_INT);
        param.registerParam("Band 3 freq",    "EQ",250,1000, 500, 0.01,     (void*)&engine->ip->bands[2].freq,  false,  IS_DOUBLE);
        param.registerParam("Band 3 gain",    "EQ", -48, 24, 0.0, 0.01,     (void*)&engine->ip->bands[2].gain,  false,  IS_DOUBLE);
        param.registerParam("Band 3 Q",       "EQ", 0.5, 10, 1.0, 0.01,     (void*)&engine->ip->bands[2].Q,     false,  IS_DOUBLE);

        param.registerParam("Band 4 enable",  "EQ",  0,   1,   1,    1,     (void*)&engine->ip->bands[3].enabled,true,  IS_INT);
        param.registerParam("Band 4 type",    "EQ",  0,   2,   1,    1,     (void*)&engine->ip->bands[3].type,   true,  IS_INT);
        param.registerParam("Band 4 mute",    "EQ",  0,   1,   0,    1,     (void*)&engine->ip->bands[3].mute,   true,  IS_INT);
        param.registerParam("Band 4 freq",    "EQ",800,3000,1500, 0.01,     (void*)&engine->ip->bands[3].freq,  false,  IS_DOUBLE);
        param.registerParam("Band 4 gain",    "EQ", -48, 24, 0.0, 0.01,     (void*)&engine->ip->bands[3].gain,  false,  IS_DOUBLE);
        param.registerParam("Band 4 Q",       "EQ", 0.7, 10, 1.2, 0.01,     (void*)&engine->ip->bands[3].Q,     false,  IS_DOUBLE);

        param.registerParam("Band 5 enable",  "EQ",  0,   1,   1,    1,     (void*)&engine->ip->bands[4].enabled,true,  IS_INT);
        param.registerParam("Band 5 type",    "EQ",  0,   2,   1,    1,     (void*)&engine->ip->bands[4].type,   true,  IS_INT);
        param.registerParam("Band 5 mute",    "EQ",  0,   1,   0,    1,     (void*)&engine->ip->bands[4].mute,   true,  IS_INT);
        param.registerParam("Band 5 freq",    "EQ",2000,8000,4500,0.01,     (void*)&engine->ip->bands[4].freq,  false,  IS_DOUBLE);
        param.registerParam("Band 5 gain",    "EQ", -48, 24, 0.0, 0.01,     (void*)&engine->ip->bands[4].gain,  false,  IS_DOUBLE);
        param.registerParam("Band 5 Q",       "EQ", 0.8, 10, 1.4, 0.01,     (void*)&engine->ip->bands[4].Q,     false,  IS_DOUBLE);
 
        param.registerParam("Band 6 enable",  "EQ",  0,   1,   1,    1,     (void*)&engine->ip->bands[5].enabled,true,  IS_INT);
        param.registerParam("Band 6 type",    "EQ",  0,   2,   2,    1,     (void*)&engine->ip->bands[5].type,   true,  IS_INT);
        param.registerParam("Band 6 mute",    "EQ",  0,   1,   0,    1,     (void*)&engine->ip->bands[5].mute,   true,  IS_INT);
        param.registerParam("Band 6 freq",    "EQ",6000,20000,10000,0.01,   (void*)&engine->ip->bands[5].freq,  false,  IS_DOUBLE);
        param.registerParam("Band 6 gain",    "EQ", -48, 24, 0.0, 0.01,     (void*)&engine->ip->bands[5].gain,  false,  IS_DOUBLE);
        param.registerParam("Band 6 Q",       "EQ", 0.4, 10, 0.7, 0.01,     (void*)&engine->ip->bands[5].Q,     false,  IS_DOUBLE);
        //                  name           group    min, max, def, step     value                               isStepped  type
        param.registerParam("Solo Band",      "EQ",  0,   5,   0,    1,     (void*)&engine->ip->solo_band,       true,  IS_INT);
        param.registerParam("Solo enabled",   "EQ",  0,   1,   0,    1,     (void*)&engine->ip->solo_enabled,    true,  IS_INT);

        param.registerParam("Lowcut enable",  "EQ",  0,   1,   0,    1,     (void*)&engine->ip->lowcut_enabled,  true,  IS_INT);
        param.registerParam("Lowcut freq",    "EQ", 35, 2200, 100,0.01,     (void*)&engine->ip->lowcut,         false,  IS_DOUBLE);
        param.registerParam("Highcut enable", "EQ",  0,   1,   0,    1,     (void*)&engine->ip->highcut_enabled, true,  IS_INT);
        param.registerParam("Highcut freq",   "EQ", 110,22000,4000,0.01,    (void*)&engine->ip->highcut,        false,  IS_DOUBLE);

        param.registerParam("Smooth",         "IR",  0,   1,  0.3, 0.01,    (void*)&engine->ip->smooth_amount,  false,  IS_DOUBLE);
        param.registerParam("Dynamics",       "IR", -1,   1,  0.0, 0.01,    (void*)&engine->ip->dynamics_amount,false,  IS_DOUBLE);
        param.registerParam("Tone Bias",      "IR", -1,   1,  0.0, 0.01,    (void*)&engine->ip->tilt_amount,    false,  IS_DOUBLE);
       
        param.registerParam("Volume Out", "Global",-46,  12,  0.0,  0.1,    (void*)&engine->vu->gain,           false,  IS_FLOAT);
    }

    // send value changes from GUI to the engine/host
    void sendValueChanged(int index, float value) {
        param.setParam(index, value);
        param.setParamDirty(index, true);
        param.controllerChanged.store(true, std::memory_order_release);
        if (index > 0 && index < 46) {
            engine->processIR.store(true, std::memory_order_release);
            engine->rebuild.store(false, std::memory_order_release);
            engine->workToDo.store(true, std::memory_order_release);
            rebuild = false;
        }
    }

    // Callbacks
    static void quit_response(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        auto* self = static_cast<SpectrumViewer*>(w->parent_struct);
        if (w->flags & HAS_POINTER && !adj_get_value(w->adj)){
            self->run = false;
            destroy_widget(self->top, self->top->app);
        }
    }

    static void set_gain(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        auto* self = static_cast<SpectrumViewer*>(w->parent_struct);
        self->sendValueChanged(46, adj_get_value(w->adj));
    }

    static void bp_response(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        auto* self = static_cast<SpectrumViewer*>(w->parent_struct);
        self->sendValueChanged(0, adj_get_value(w->adj));
    }

    void set_radio(Widget_t* w, bool set) {
         for (int i = 0; i<6; i++) {
            Widget_t *wid = solo[i];
            if (wid != w) {
                xevfunc store = wid->func.value_changed_callback;
                wid->func.value_changed_callback = null_callback;
                adj_set_value(wid->adj, 0.0);
                engine->ip->setSoloBand(wid->data, (int)adj_get_value(wid->adj));
                wid->func.value_changed_callback = store;
            }
        }
        if (set) {
            Widget_t * p = (Widget_t*)w->parent;
            int i = 0;
            for(;i<p->childlist->elem;i++) {
                Widget_t *wid = p->childlist->childs[i];
                if (wid->adj && wid->flags & IS_RADIO) {
                    xevfunc store = wid->func.value_changed_callback;
                    wid->func.value_changed_callback = null_callback;
                    if (wid != w) adj_set_value(wid->adj, 0.0);
                    engine->ip->setMuteBand(wid->data, (int)adj_get_value(wid->adj));
                    wid->func.value_changed_callback = store;
                }
            }
        }
    }

    static void solo_response(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        auto* self = static_cast<SpectrumViewer*>(w->parent_struct);
        self->set_radio(w, true);
        self->sendValueChanged(37, w->data);
        self->sendValueChanged(38, adj_get_value(w->adj));
    }

    static void mute_response(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        auto* self = static_cast<SpectrumViewer*>(w->parent_struct);
        self->set_radio(w, false);
        self->sendValueChanged(3 + (6 * w->data), adj_get_value(w->adj));
    }

    static void set_ftype(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        auto* self = static_cast<SpectrumViewer*>(w->parent_struct);
        self->sendValueChanged(2 + (6 * w->data), adj_get_value(w->adj));
    }

    static void set_freq(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        auto* self = static_cast<SpectrumViewer*>(w->parent_struct);
        self->sendValueChanged(4 + (6 * w->data), adj_get_value(w->adj));
    }

    static void set_fq(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        auto* self = static_cast<SpectrumViewer*>(w->parent_struct);
        self->sendValueChanged(6 + (6 * w->data), adj_get_value(w->adj));
    }

    static void set_fgain(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        auto* self = static_cast<SpectrumViewer*>(w->parent_struct);
        self->sendValueChanged(5 + (6 * w->data), adj_get_value(w->adj));
    }

    static void set_fenable(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        auto* self = static_cast<SpectrumViewer*>(w->parent_struct);
        self->sendValueChanged(1 + (6 * w->data), adj_get_value(w->adj));
    }

    static void set_lowcut(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        auto* self = static_cast<SpectrumViewer*>(w->parent_struct);
        self->sendValueChanged(40, adj_get_value(w->adj));
    }

    static void set_lowcut_enable(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        auto* self = static_cast<SpectrumViewer*>(w->parent_struct);
        self->sendValueChanged(39, adj_get_value(w->adj));
    }

    static void set_highcut(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        auto* self = static_cast<SpectrumViewer*>(w->parent_struct);
        self->sendValueChanged(42, adj_get_value(w->adj));
    }

    static void set_highcut_enable(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        auto* self = static_cast<SpectrumViewer*>(w->parent_struct);
        self->sendValueChanged(41, adj_get_value(w->adj));
    }

    static void set_smooth(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        auto* self = static_cast<SpectrumViewer*>(w->parent_struct);
        self->sendValueChanged(43, adj_get_value(w->adj));
    }

    static void set_dynamics(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        auto* self = static_cast<SpectrumViewer*>(w->parent_struct);
        self->sendValueChanged(44, adj_get_value(w->adj));
    }

    static void set_tilt(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        auto* self = static_cast<SpectrumViewer*>(w->parent_struct);
        self->sendValueChanged(45, adj_get_value(w->adj));
    }

    void comput_and_set() {
        rebuild = true;
        engine->processIR.store(true, std::memory_order_release);
        engine->rebuild.store(true, std::memory_order_release);
        engine->workToDo.store(true, std::memory_order_release);
    }

    static void set_irsize(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        auto* self = static_cast<SpectrumViewer*>(w->parent_struct);
        int v = (int)adj_get_value(w->adj);
        switch (v) {
            case 0: self->irLength = 1024;
            break;
            case 1: self->irLength = 2048;
            break;
            case 2: self->irLength = 4096;
            break;
            case 3: self->irLength = 8192;
            break;
            case 4: self->irLength = 16384;
            break;
            default : self->irLength = 2048;
            break;
        }
        // adjust minimum lowcut val
        float min_lc = self->get_min_lowcut(self->sampleRate, self->irLength);
        float lc = adj_get_value(self->lowcut->adj);
        float val = std::max<float>(lc, min_lc);
        set_adjustment(self->lowcut->adj, 100.0, val, min_lc, 2200.0, 0.01, CL_LOGARITHMIC);
        self->engine->ip->setLowCut((double)adj_get_value(self->lowcut->adj));
        expose_widget(self->lowcut);
        // recompute spectrum with new size
        self->comput_and_set();
    }

    static void set(void *w_, void *event, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        auto* self = static_cast<SpectrumViewer*>(w->parent_struct);
        self->set_leak.store(true, std::memory_order_release);
    }

    static void ref_load_response(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        auto* self = static_cast<SpectrumViewer*>(w->parent_struct);
        if(user_data !=NULL) {
            self->ref_file = *(const char**)user_data;
            if ( self->af.getAudioFile(self->ref_file, self->sampleRate) ) {
                self->dstL.clear();
                self->dstR.clear();
                self->dstL.assign(self->af.samplesL.begin(), self->af.samplesL.end());
                self->dstR.assign(self->af.samplesR.begin(), self->af.samplesR.end());
                self->comput_and_set();
                self->ref_label->label = self->ref_file.data();
                expose_widget(self->ref_label);
            }
        }
    }

    static void remove_ref_response(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        if (w->flags & HAS_POINTER && !adj_get_value(w->adj)){
            auto* self = static_cast<SpectrumViewer*>(w->parent_struct);
            self->dstL.clear();
            self->dstR.clear();
            self->ref_label->label = " ";
            expose_widget(self->ref_label);
            self->comput_and_set();
        }
    }

    static void src_load_response(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        auto* self = static_cast<SpectrumViewer*>(w->parent_struct);
        if(user_data !=NULL) {
            self->src_file = *(const char**)user_data;
            if ( self->af.getAudioFile(self->src_file, self->sampleRate) ) {
                self->srcL.clear();
                self->srcR.clear();
                self->srcL.assign(self->af.samplesL.begin(), self->af.samplesL.end());
                self->srcR.assign(self->af.samplesR.begin(), self->af.samplesR.end());
                self->comput_and_set();
                self->src_label->label = self->src_file.data();
                expose_widget(self->src_label);
            }
        }
    }

    static void remove_src_response(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        if (w->flags & HAS_POINTER && !adj_get_value(w->adj)){
            auto* self = static_cast<SpectrumViewer*>(w->parent_struct);
            self->srcL.clear();
            self->srcR.clear();
            self->src_label->label = " ";
            expose_widget(self->src_label);
            self->comput_and_set();
        }
    }

    static void save_response(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        auto* self = static_cast<SpectrumViewer*>(w->parent_struct);
        if(user_data !=NULL) {
            self->ir_file = *(const char**)user_data;
            std::pair<std::vector<double>, std::vector<double> > ir = self->engine->ip->createIRStereo();        
            self->af.saveAudioFile(self->ir_file, ir.first, ir.second, self->sampleRate);
            std::cout << "save as: " << self->ir_file << std::endl;
        }
    }

    static void mouse_set_spec(void *w_, void *xbutton_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        auto* self = static_cast<SpectrumViewer*>(w->parent_struct);
        XButtonEvent *xbutton = (XButtonEvent*)xbutton_;
        if (w->flags & HAS_POINTER) {
            if(xbutton->button == Button1) {
                self->mx = xbutton->x;
                self->my = xbutton->y;
            }
        }
    }

    void infoString(float x, float y) {
        Metrics_t m;
        os_get_window_metrics(spec, &m);
        const int width  = m.width;
        const int height = m.height;

        float freq = x_to_freq(x, f_min, f_max, width);
        float g = y_to_db(y, db_min, db_max, height);
        if (freq >= 10000.0f)
            snprintf(cfreq, 63, "%.1f kHz", freq / 1000.0);
        else if (freq >= 1000.0f)
            snprintf(cfreq, 63, "%.2f kHz", freq / 1000.0);
        else if (freq >= 100.0f)
            snprintf(cfreq, 63, "%.1f Hz", freq );
        else
            snprintf(cfreq, 63, " %.1f Hz", freq);

        if (g > 10.0f) 
            snprintf(cgain, 63, " %.1f dB", g);
        else if (g > -0.001f) 
            snprintf(cgain, 63, "  %.1f dB", g);
        else if (g > -10.0f) 
            snprintf(cgain, 63, " %.1f dB", g);
        else
            snprintf(cgain, 63, "%.1f dB", g);
        curFreq->label = cfreq;
        curGain->label = cgain;
        expose_widget(curFreq);
        expose_widget(curGain);
    }

    static void mouse_leave_spec(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        auto* self = static_cast<SpectrumViewer*>(w->parent_struct);
        self->curFreq->label = "";
        self->curGain->label = "";
        expose_widget(self->curFreq);
        expose_widget(self->curGain);
    }

    static void mouse_in_spec(void *w_, void *xmotion_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        XMotionEvent *xmotion = (XMotionEvent*)xmotion_;
        auto* self = static_cast<SpectrumViewer*>(w->parent_struct);
        int x1 = xmotion->x;
        int y1 = xmotion->y;
        self->match_state = 0;
        self->infoString(x1, y1);
        //std::cout << "x " << x1 << " y " << y1 << std::endl;
        if(xmotion->state & Button1Mask) {
            self->match_state = 1;
            if (self->band_match) {
                float v = adj_get_value(self->freq[self->match_band]->adj);
                float deltaX = (float)x1 - self->mx;
                v *= std::pow(2.0, deltaX * 0.005);
                self->mx = x1;
                adj_set_value(self->freq[self->match_band]->adj, v);

                float vg = adj_get_value(self->fgain[self->match_band]->adj);
                float deltay = (float)y1 - self->my;
                vg += deltay * -0.1;
                self->my = y1;
                if (std::abs(vg) < 0.2) vg = 0.0;
                adj_set_value(self->fgain[self->match_band]->adj, vg);
                expose_widget(self->spec);
            }
        } else {
            self->find_hovered_band(x1, y1);
        }
    }

    static void mouse_move_spec(void *w_, void *xbutton_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        auto* self = static_cast<SpectrumViewer*>(w->parent_struct);
        XButtonEvent *xbutton = (XButtonEvent*)xbutton_;
        if (w->flags & HAS_POINTER) {
            if (self->band_match) {
                if(xbutton->button == Button4) {
                    float vq = adj_get_value(self->fq[self->match_band]->adj);
                    vq *= std::pow(2.0, 0.1);
                    adj_set_value(self->fq[self->match_band]->adj,vq);
                    expose_widget(self->spec);
                } else if(xbutton->button == Button5) {
                    float vq = adj_get_value(self->fq[self->match_band]->adj);
                    vq *= std::pow(2.0, -0.1);
                    adj_set_value(self->fq[self->match_band]->adj,vq);
                    expose_widget(self->spec);
                }
            }
        }

    }

    // Helpers
    float get_min_lowcut(float sr, size_t ir_len) {
        float fmin = sr / (float)ir_len;
        return std::max<float>(20.0f,fmin * 1.5f);
    }

    static float clampf(float x, float lo, float hi) {
        return (x < lo) ? lo : (x > hi) ? hi : x;
    }

    static float db_to_y(float db, float db_min, float db_max, int height) {
        float norm = (db - db_min) / (db_max - db_min);
        norm = clampf(norm, 0.0f, 1.0f);
        return (1.0f - norm) * height;
    }

    static float freq_to_x(float freq, float f_min, float f_max, int width) {
        const float x_pad = 3.0f;
        const float inv_log_range = 1.0f / log10f(f_max / f_min);

        freq = std::clamp(freq, f_min, f_max);

        float norm = log10f(freq / f_min) * inv_log_range;
        return x_pad + norm * (width - 2.0f * x_pad);
    }

    static void draw_text(cairo_t* cr, float x, float y, const char* txt) {
        cairo_move_to(cr, std::max<float>(5.0f, x), y);
        cairo_show_text(cr, txt);
    }

    // Drawing

    struct Theme {
        // background
        double bg_r = 0.08;
        double bg_g = 0.09;
        double bg_b = 0.11;

        // grid
        double grid_major_r = 0.35;
        double grid_major_g = 0.38;
        double grid_major_b = 0.42;

        double grid_minor_r = 0.22;
        double grid_minor_g = 0.24;
        double grid_minor_b = 0.28;

        // text
        double text_r = 0.8;
        double text_g = 0.82;
        double text_b = 0.85;

        double text_dim_r = 0.5;
        double text_dim_g = 0.52;
        double text_dim_b = 0.55;

        // spectrum
        double spec_alpha = 0.35;

        // band fill / glow
        double band_fill_alpha = 0.22;
        double band_glow_alpha = 0.08;
        double band_line_alpha = 0.95;
    };
    Theme t;

    static void draw_callback(void* w_, void* user_data) {
        Widget_t* w = (Widget_t*)w_;
        auto* self = static_cast<SpectrumViewer*>(w->parent_struct);
        self->draw(w_);
    }

    static void get_band_color(int i, double &r, double &g, double &b) {
        if (i == 0) {
            r = 0.95;
            g = 0.55;
            b = 0.20;
        } else if (i == 1) {
            r = 0.95;
            g = 0.80;
            b = 0.25;
        } else if (i == 2) {
            r = 0.40;
            g = 0.85;
            b = 0.35;
        } else if (i == 3) {
            r = 0.25;
            g = 0.80;
            b = 0.75;
        } else if (i == 4) {
            r = 0.30;
            g = 0.55;
            b = 0.95;
        } else {
            r = 0.75;
            g = 0.40;
            b = 0.95;
         }
    }

    static double mapQ(double q_ui) {
        // clamp UI range
        q_ui = std::clamp(q_ui, 0.1, 10.0);
        // log-space mapping
        double x = std::log(q_ui);
        // soften curve
        double shaped = std::tanh(x * 0.8);
        // back to linear
        double q = std::exp(shaped * 1.5);
        return q;
    }

    static float y_to_db(float y, float db_min, float db_max, int height) {
        float norm = 1.0f - (y / height);
        norm = clampf(norm, 0.0f, 1.0f);
        return db_min + norm * (db_max - db_min);
    }

    static inline double x_to_freq(double x, double f_min, double f_max, int width) {
        double t = x / (double)width;
        // log interpolation
        double log_min = std::log(f_min);
        double log_max = std::log(f_max);
        double log_f = log_min + t * (log_max - log_min);
        return std::exp(log_f);
    }

    static double eval_band_db(const Band& b, double f, double sr) {
        if (f < 10.0) return 0.0;
        double x = std::log2((f + 1e-9) / (b.freq + 1e-9));
        double Q = mapQ(b.Q);

        switch (b.type) {
            case Band::Peak: {
                double sigma = 1.0 / (1.5 * Q + 0.5);
                double g = std::exp(-0.5 * (x * x) / (sigma * sigma));
                return b.gain * g;
            }
            case Band::LowShelf: {
                double slope = Q * 2.0;
                double g = 0.5 * (1.0 - std::tanh(slope * x));
                return b.gain * g;
            }
            case Band::HighShelf: {
                double slope = Q * 2.0;
                double g = 0.5 * (1.0 + std::tanh(slope * x));
                return b.gain * g;
            }
        }
        return 0.0;
    }

    void draw_band_ring(cairo_t* cr, float x, float y, int i, int state) {
        double r,g,bcol;
        get_band_color(i, r, g, bcol);
        double radius = 6.0;
        double ring   = 10.0;
        double alpha  = 1.0;

        if (state == 0) {
            radius = 7.5;
            ring   = 14.0;
            alpha  = 0.5;
        }
        else if (state == 1) {
            radius = 8.5;
            ring   = 18.0;
            alpha  = 0.6;
        }

        // glow
        cairo_arc(cr, x, y, ring, 0, 2*M_PI);
        cairo_set_source_rgba(cr, r, g, bcol, 0.15);
        cairo_fill(cr);

        // ring
        cairo_arc(cr, x, y, radius + 3, 0, 2*M_PI);
        cairo_set_line_width(cr, 2.0);
        cairo_set_source_rgba(cr, r, g, bcol, 0.9 * alpha);
        cairo_stroke(cr);

        // center dot
        cairo_arc(cr, x, y, radius, 0, 2*M_PI);
        cairo_set_source_rgba(cr, r, g, bcol, 1.0 * alpha);
        cairo_fill(cr);
    }

    void find_hovered_band(float mx, float my) {
        Metrics_t m;
        os_get_window_metrics(spec, &m);
        const int width  = m.width;
        const int height = m.height;
        for (int i = 0; i < 6; ++i) {
            float x = freq_to_x(engine->ip->bands[i].freq, f_min, f_max, width);
            float y = db_to_y(engine->ip->bands[i].gain, db_min, db_max, height);

            float dx = mx - x;
            float dy = my - y;

            if (dx*dx + dy*dy < 12*12) {
                band_match = true;
                match_band = i;
                rebuild_eq_layer = true;
                expose_widget(spec);
                return ;
            } else if (band_match) {
                band_match = false;
                rebuild_eq_layer = true;
                expose_widget(spec);
            }
        }
        band_match = false;
    }

    void draw_band_curves(cairo_t* cr, int width, int height) {
        const int STEPS = width;
        float y0 = db_to_y(0.0, db_min, db_max, height);

        for (int i = 0; i < 6; ++i) {
            auto& b = engine->ip->bands[i];
            if (!b.enabled) continue;
            bool isStarted = false;
            double startX = 0.0;
            double stopX = 0.0;
            double lastX = 0.0;
            // band color
            double r,g,bcol;
            get_band_color(i, r, g, bcol);
            cairo_new_path(cr);
            for (int x = 0; x < STEPS; ++x) {
                double freq = x_to_freq(x, f_min, f_max, width);
                double db = eval_band_db(b, freq, sampleRate);
                double y = db_to_y(db, db_min, db_max, height);
                if (fabs(y - y0) < 0.5) continue;

                if (!isStarted) {
                    cairo_move_to(cr, x, y);
                    startX = x;
                    lastX = x;
                    isStarted = true;
                } else if (x > lastX + 2.0) {
                    cairo_line_to(cr, x, y);
                }
                stopX = x;
            }

            // fill
            cairo_line_to(cr, stopX, y0);
            cairo_line_to(cr, startX, y0);
            cairo_close_path(cr);

            cairo_set_source_rgba(cr, r, g, bcol, t.band_fill_alpha);
            cairo_fill_preserve(cr);
            // glow
            cairo_pattern_t* glow = cairo_pattern_create_linear(startX, 0, stopX, 0);
            cairo_pattern_add_color_stop_rgba(glow, 0, r, g, bcol,t.band_glow_alpha * 0.1);
            cairo_pattern_add_color_stop_rgba(glow, 0.5, r, g, bcol,t.band_glow_alpha * 0.8);
            cairo_pattern_add_color_stop_rgba(glow, 1, r, g, bcol,t.band_glow_alpha * 0.1);
            for (int k = 0; k < 3; ++k) {
                double width_glow = 6.0 + k * 4.0;

                cairo_set_line_width(cr, width_glow);
                cairo_set_source(cr, glow);
                //cairo_set_source_rgba(cr, r, g, bcol, t.band_fill_alpha * 0.5);
                cairo_stroke_preserve(cr);
            }
            cairo_pattern_destroy(glow);

            // line
            cairo_pattern_t* grad = cairo_pattern_create_linear(startX, 0, stopX, 0);
            cairo_pattern_add_color_stop_rgba(grad, 0, r, g, bcol,t.band_line_alpha * 0.1);
            cairo_pattern_add_color_stop_rgba(grad, 0.5, r, g, bcol,t.band_line_alpha * 1);
            cairo_pattern_add_color_stop_rgba(grad, 1, r, g, bcol,t.band_line_alpha * 0.1);
            cairo_set_line_width(cr, 1.5);
            //cairo_set_source_rgba(cr, r, g, bcol, t.band_line_alpha);
            cairo_set_source(cr, grad);
            cairo_stroke(cr);
            cairo_pattern_destroy(grad);
        }
    }

    void draw_band_points(cairo_t* cr, const int width, const int height) {
        cairo_set_line_width(cr, 10.0);
        for(int i = 0; i<6; i++) {
            double r,g,bcol;
            get_band_color(i, r, g, bcol);
            cairo_set_source_rgba(cr, r, g, bcol, 1.0);

            int on = engine->ip->bands[i].enabled;
            float db = db_to_y(engine->ip->bands[i].gain, db_min, db_max, height);
            float freq = freq_to_x(engine->ip->bands[i].freq, f_min, f_max, width);
            if (on) {
                cairo_move_to(cr, freq, db);
                cairo_line_to(cr, freq, db);
                cairo_stroke(cr);
            }
        }
        for(int i = 0; i<6; i++) {
            int on = engine->ip->bands[i].enabled;
            float db = db_to_y(engine->ip->bands[i].gain, db_min, db_max, height);
            float freq = freq_to_x(engine->ip->bands[i].freq, f_min, f_max, width);
            if (on) {
                if (band_match && (match_band == i)) {
                    draw_band_ring(cr, freq, db, i, match_state);
                }
            }
        }
    }

    void create_background(Widget_t *w, const int width, const int height) {
        std::vector<double> freqs = {20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000};
        std::vector<double> minor_freqs = {30, 40, 60, 70, 80, 90, 300, 400, 600, 700, 800,
                                                    900, 3000, 4000, 6000, 7000, 8000, 9000};
        std::vector<double> dbs = {-72, -48, -24, -18, -12, -6, 0, 6, 12, 18, 24};

        if (w->image) cairo_surface_destroy(w->image);
        w->image = nullptr;
        w->image = cairo_surface_create_similar (w->surface,
                            CAIRO_CONTENT_COLOR_ALPHA, width, height);
        if (!w->image || cairo_surface_status(w->image) != CAIRO_STATUS_SUCCESS) {
            w->image = nullptr;
            return;
        }
        cairo_t *cr = cairo_create (w->image);
        if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
            cairo_destroy(cr);
            return;
        }
        cairo_set_source_rgb(cr, t.bg_r, t.bg_g, t.bg_b);
        cairo_rectangle(cr, 0, 0, width, height);
        cairo_fill(cr);

        cairo_pattern_t* grad = cairo_pattern_create_linear(0, 0, 0, height);
        cairo_pattern_add_color_stop_rgba(grad, 0, 1,1,1,0.04);
        cairo_pattern_add_color_stop_rgba(grad, 1, 0,0,0,0.1);

        cairo_rectangle(cr, 0, 0, width, height);
        cairo_set_source(cr, grad);
        cairo_fill(cr);

        cairo_pattern_destroy(grad);

        cairo_set_source_rgba(cr, 0.267, 0.267, 0.267, 0.8);
        cairo_set_line_width(cr, 1.0);

        // Frequency lines
        for (double f : freqs) {
            double x = freq_to_x(f, f_min, f_max, width);
            bool major = (f == 100 || f == 1000 || f == 10000);
            cairo_set_source_rgba(
                cr,
                major ? t.grid_major_r : t.grid_minor_r,
                major ? t.grid_major_g : t.grid_minor_g,
                major ? t.grid_major_b : t.grid_minor_b,
                major ? 0.5 : 0.25
            );

            cairo_set_line_width(cr, major ? 1.5 : 1.0);
            cairo_move_to(cr, x, 0);
            cairo_line_to(cr, x, height);
            cairo_stroke(cr);
        }
        for (double f : minor_freqs) {
            double x = freq_to_x(f, f_min, f_max, width);
            cairo_set_source_rgba(
                cr, t.grid_minor_r, t.grid_minor_g, t.grid_minor_b, 0.1 );

            cairo_set_line_width(cr, 1.0);
            cairo_move_to(cr, x, 0);
            cairo_line_to(cr, x, height);
            cairo_stroke(cr);
        }

        // dB lines
        for (double db : dbs) {
            double y = db_to_y(db, db_min, db_max, height);
            bool major = (db == 0);
            cairo_set_source_rgba(
                cr,
                major ? t.grid_major_r : t.grid_minor_r,
                major ? t.grid_major_g : t.grid_minor_g,
                major ? t.grid_major_b : t.grid_minor_b,
                major ? 0.6 : 0.25
            );

            cairo_set_line_width(cr, major ? 1.5 : 1.0);
            cairo_move_to(cr, 0, y);
            cairo_line_to(cr, width, y);
            cairo_stroke(cr);
        }
        // frequency labels
        for (double f : freqs) {
            double x = freq_to_x(f, f_min, f_max, width);
            char buf[32];
            if (f >= 1000)
                sprintf(buf, "%.0fk", f / 1000.0);
            else
                sprintf(buf, "%.0f", f);
            cairo_set_source_rgba(cr, t.text_dim_r, t.text_dim_g, t.text_dim_b, 0.7);
            cairo_move_to(cr, x + 4, height - 6);
            cairo_show_text(cr, buf);
        }

        // dB labels
        for (double db : dbs) {
            double y = db_to_y(db, db_min, db_max, height);
            char buf[16];
            sprintf(buf, "%.0f", db);
            cairo_set_source_rgba(cr, t.text_dim_r, t.text_dim_g, t.text_dim_b, 0.7);
            cairo_move_to(cr, 5, y - 2);
            cairo_show_text(cr, buf);
        }
        cairo_destroy(cr);
    }

    void create_layer(Widget_t *w, const int width, const int height, const float sample_rate) {
        if(image_layer) cairo_surface_destroy(image_layer);
        image_layer = nullptr;
        image_layer = cairo_surface_create_similar (w->surface,
                            CAIRO_CONTENT_COLOR_ALPHA, width, height);
        if (!image_layer || cairo_surface_status(image_layer) != CAIRO_STATUS_SUCCESS) {
            image_layer = nullptr;
            return;
        }
        cairo_t *cr = cairo_create (image_layer);
        if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
            cairo_destroy(cr);
            return;
        }
        cairo_set_source_surface (cr, w->image, 0, 0);
        cairo_rectangle(cr,0, 0, width, height);
        cairo_fill(cr);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        drawSpectrum(cr, ref_,   width, height, 1.5, sample_rate, 1.0, 0.333, 0.333,   "reference",   height-20);
        drawSpectrum(cr, source_,width, height, 1.5, sample_rate, 0.314, 0.98, 0.482,  "source",      height-40);
        drawSpectrum(cr, diff_,  width, height, 1.5, sample_rate, 1.0, 0.722, 0.424,   "diff",  height-60, true);
        cairo_destroy(cr);
        rebuild_layer = false;
    }

    void create_eq_layer(Widget_t *w, const int width, const int height, const float sample_rate) {
        if (spec_height != height || spec_width != width) {
            if(eq_layer) cairo_surface_destroy(eq_layer);
            eq_layer = nullptr;
            eq_layer = cairo_surface_create_similar (w->surface,
                                CAIRO_CONTENT_COLOR_ALPHA, width, height);
            if (!eq_layer || cairo_surface_status(eq_layer) != CAIRO_STATUS_SUCCESS) {
                eq_layer = nullptr;
                return;
            }
        }
        cairo_t *cr = cairo_create (eq_layer);
        if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
            cairo_destroy(cr);
            return;
        }
        cairo_set_source_surface (cr, image_layer, 0, 0);
        cairo_rectangle(cr,0, 0, width, height);
        cairo_fill(cr);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        draw_band_points(cr, width, height);
        draw_band_curves(cr, width, height);
        drawSpectrum(cr, ir_,    width, height, 2.5, sample_rate, 0.545, 0.914, 0.992, "impulse",      height-80);
        cairo_destroy(cr);
        rebuild_eq_layer = false;
    }

    void draw(void* w_) {
        Widget_t* w = (Widget_t*)w_;

        Metrics_t m;
        os_get_window_metrics(w, &m);
        if (!m.visible) return;

        cairo_t* cr = w->crb;

        const float sample_rate = sampleRate;

        const int width  = m.width;
        const int height = m.height;
        if (spec_height != height || spec_width != width) {
            create_background(w, width, height);
            create_layer(w, width, height, sample_rate);
            create_eq_layer(w, width, height, sample_rate);
        }
        if (!image_layer || rebuild_layer) {
            create_layer(w, width, height, sample_rate);
        }
        if (!eq_layer || rebuild_eq_layer) {
            create_eq_layer(w, width, height, sample_rate);
        }
        if (eq_layer) {
            cairo_set_source_surface (w->crb, eq_layer, 0, 0);
            cairo_rectangle(w->crb,0, 0, width, height);
            cairo_fill(w->crb);
        } else {
            create_background(w, width, height);
            create_layer(w, width, height, sample_rate);
            create_eq_layer(w, width, height, sample_rate);
            return;
        }
        spec_height = height;
        spec_width = width;

        drawSpectrum(cr, mag_, width, height, 1.5, sample_rate, 0.45, 0.2, 0.75, "input", height-100, false, true);

        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 11);

    }

    void drawSpectrum(cairo_t* cr, const Vec& mags, int width, int height, double line_width,
                      float sample_rate, float r, float g, float b, const char* label,
                      float label_y, bool dash = false, bool fill = false) {

        if (mags.empty()) return;

        cairo_set_source_rgba(cr, r, g, b, t.spec_alpha);
        draw_text(cr, width - 60, label_y, label);

        cairo_set_line_width(cr, line_width);
        static const double dashes[] = {2.0};
        if (dash) {
            cairo_set_dash(cr, dashes, 1, 0);
            cairo_set_line_width(cr, 1.0);
        } else {
            cairo_set_dash(cr, dashes, 0, 0);
        }

        int bins = mags.size();
        int fft_size = bins * 2;

        bool started = false;

        float last_x = -1.0f;
        for (int i = 1; i < bins; ++i) {
            float freq = (float)i * sample_rate / fft_size;
            if (freq < f_min || freq > f_max) continue;

            float x = freq_to_x(freq, f_min, f_max, width);
            float y = db_to_y(mags[i], db_min, db_max, height);

            if (!started) {
                cairo_move_to(cr, 3, y);
                started = true;
            } else {
                if (x > last_x + 0.8f) {
                    cairo_line_to(cr, x, y);
                    last_x = x;
                }
            }
        }
        cairo_stroke_preserve(cr);
    
        // Spectrum fill
        if (started && fill) {
            //cairo_set_source_rgba(cr,  0.17, 0.82, 0.64, 0.15);
            cairo_line_to(cr, width, height);
            cairo_line_to(cr, 3, height);
            cairo_close_path(cr);
            cairo_pattern_t* pat = cairo_pattern_create_linear(0, 0, 0, height);
            cairo_pattern_add_color_stop_rgba(pat, 0.0, 0.75, 0.2, 0.9, 0.25);
            cairo_pattern_add_color_stop_rgba(pat, 1.0, 0.45, 0.2, 0.75, 0.05);
            cairo_set_source(cr, pat);
            cairo_fill(cr);
            cairo_pattern_destroy(pat);
        }
        cairo_stroke(cr);
    }
};
