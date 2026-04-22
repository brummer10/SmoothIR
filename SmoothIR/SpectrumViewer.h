
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
#include "AudioFile.h"
#include "xwidgets.h"
#include "widgets.cc"

class SpectrumViewer {
public:
    using Vec = std::vector<float>;
    std::vector<double> dstf;
    std::vector<double> srcf;
    std::string ref_file;
    std::string src_file;

    SpectrumViewer(IRProcessor *ip_) {
        ip = ip_;
    }

    ~SpectrumViewer() {}

    void setRef(const std::vector<double>& ref, std::string ref_file_) {
        dstf.assign(ref.begin(), ref.end());
        ref_file = ref_file_;
    }

    void setSource(const std::vector<double>& source, std::string src_file_) {
        srcf.assign(source.begin(), source.end());
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

    void show(int width = 660, int height = 420) {
        Xputty main;
        main_init(&main);

        top = create_window(&main,
            os_get_root_window(&main, IS_WINDOW),
            0, 0, width, height);
        widget_set_title(top, "Smoothed IR");
        //top->flags = NO_PROPAGATE;
        top->func.expose_callback = draw_window;

        spec = create_widget(&main, top,0, 0, width, height-90);
        spec->parent_struct = this;
        spec->func.expose_callback = draw_callback;

        Widget_t* ref = add_my_file_button(top, 10, 345, 90, 20, "Reference:", " ", ".wav|.WAV");
        ref->flags |= USE_TRANSPARENCY | FAST_REDRAW;
        ref->parent_struct = this;
        ref->func.user_callback = ref_load_response;
        ref_label = add_my_label(top, "",100,347,260,20);
        ref_label->label = ref_file.data();

        Widget_t* src = add_my_file_button(top, 10, 380, 90, 20, "Source:", " ", ".wav|.WAV");
        src->flags |= USE_TRANSPARENCY | FAST_REDRAW;
        src->parent_struct = this;
        src->func.user_callback = src_load_response;
        src_label = add_my_label(top, "",100,382,260,20);
        src_label->label = src_file.data();

        lowcut = add_knob(top, "LowCut", 370,335,60, 80);
        float min_lc = get_min_lowcut(sampleRate, irLength);
        set_adjustment(lowcut->adj, 100.0, 100.0, min_lc, 2200.0, 0.01, CL_LOGARITHMIC);
        lowcut->flags |= USE_TRANSPARENCY | FAST_REDRAW;
        set_widget_color(lowcut, (Color_state)0, (Color_mod)0, 0.15, 0.52, 0.55, 1.0);
        lowcut->parent_struct = this;
        lowcut->func.value_changed_callback = set_lowcut;
        lowcut->func.button_release_callback = set;

        Widget_t* highcut = add_knob(top, "HighCut", 440,335,60, 80);
        set_adjustment(highcut->adj, 4000.0, 4000.0, 110.0, 22000.0, 0.01, CL_LOGARITHMIC);
        highcut->flags = USE_TRANSPARENCY | FAST_REDRAW;
        set_widget_color(highcut, (Color_state)0, (Color_mod)0, 0.15, 0.52, 0.55, 1.0);
        highcut->parent_struct = this;
        highcut->func.value_changed_callback = set_highcut;
        highcut->func.button_release_callback = set;

        Widget_t* smooth = add_knob(top, "Smooth", 510,335,60, 80);
        set_adjustment(smooth->adj, 0.5, 0.5, 0.0, 1.0, 0.01, CL_CONTINUOS);
        smooth->flags |= USE_TRANSPARENCY | FAST_REDRAW;
        set_widget_color(smooth, (Color_state)0, (Color_mod)0, 0.15, 0.52, 0.55, 1.0);
        smooth->parent_struct = this;
        smooth->func.value_changed_callback = set_smooth;
        smooth->func.button_release_callback = set;

        Widget_t* irsize = add_my_combobox(top, "Ir size", 580, 335, 60, 20);
        irsize->parent_struct = this;
        combobox_add_entry(irsize,"1024");
        combobox_add_entry(irsize,"2048");
        combobox_add_entry(irsize,"4096");
        combobox_add_entry(irsize,"8192");
        combobox_add_entry(irsize,"16384");
        combobox_set_active_entry(irsize, 1);
        irsize->func.value_changed_callback = set_irsize;
        add_tooltip(irsize->childlist->childs[0], "Set Ir length");

        Widget_t* save = add_xsave_file_button(top, 580, 365, 60, 20, "Save IR", " ", ".wav|.WAV");
        save->flags = USE_TRANSPARENCY | FAST_REDRAW;
        save->parent_struct = this;
        save->func.user_callback = save_response;

        Widget_t* quit = add_my_button(top, 580, 395, 60, 20, "Quit");
        quit->flags |= USE_TRANSPARENCY | FAST_REDRAW;
        quit->parent_struct = this;
        quit->func.value_changed_callback = quit_response;

        widget_show_all(top);
        main_run(&main);
        main_quit(&main);
    }

private:
    Widget_t* top = nullptr;
    Widget_t* ref_label = nullptr;
    Widget_t* src_label = nullptr;
    Widget_t* spec = nullptr;
    Widget_t* lowcut = nullptr;
    IRProcessor *ip;
    AudioFile af;
    Vec ref_;
    Vec source_;
    Vec diff_;
    Vec ir_;
    std::string ir_file;
    size_t irLength = 2048;
    double sampleRate = 48000.0;

    const float f_min = 20.0f;
    const float f_max = 20000.0f;
    const float db_min = -108.0f;
    const float db_max = 6.0f;

    // Callbacks
    static void quit_response(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        auto* self = static_cast<SpectrumViewer*>(w->parent_struct);
        if (w->flags & HAS_POINTER && !adj_get_value(w->adj)){
            destroy_widget(self->top, self->top->app);
        }
    }

    static void set_lowcut(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        auto* self = static_cast<SpectrumViewer*>(w->parent_struct);
        self->ip->setLowCut((double)adj_get_value(w->adj));
    }

    static void set_highcut(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        auto* self = static_cast<SpectrumViewer*>(w->parent_struct);
        self->ip->setHighCut((double)adj_get_value(w->adj));
    }

    static void set_smooth(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        auto* self = static_cast<SpectrumViewer*>(w->parent_struct);
        self->ip->setSmooth((double)adj_get_value(w->adj));
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
        self->ip->setLowCut((double)adj_get_value(self->lowcut->adj));
        // recompute spectrum with new size
        self->ip->computeIR(self->dstf, self->srcf, self->sampleRate, self->irLength);
        self->setData(self->ip->getRefMag(), self->ip->getSrcMag(), 
                        self->ip->getDiffMag(), self->ip->getIRMag());
        // show results
        expose_widget(self->lowcut);
        expose_widget(self->spec);
    }

    static void set(void *w_, void *event, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        auto* self = static_cast<SpectrumViewer*>(w->parent_struct);
        self->ip->aplayFilter();
        self->setData(self->ip->getRefMag(), self->ip->getSrcMag(), 
                        self->ip->getDiffMag(), self->ip->getIRMag());
        expose_widget(self->spec);
    }

    static void ref_load_response(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        auto* self = static_cast<SpectrumViewer*>(w->parent_struct);
        if(user_data !=NULL) {
            self->ref_file = *(const char**)user_data;
            self->ref_label->label = self->ref_file.data();
            expose_widget(self->ref_label);
            if ( self->af.getAudioFile(self->ref_file.c_str(), self->sampleRate) ) {
                self->dstf.clear();
                for (uint32_t i = 0; i < self->af.samplesize; i++) {
                    self->dstf.push_back((double)self->af.samples[i]);
                }
                self->ip->computeIR(self->dstf, self->srcf, self->sampleRate, self->irLength);
                self->setData(self->ip->getRefMag(), self->ip->getSrcMag(), 
                                self->ip->getDiffMag(), self->ip->getIRMag());
                expose_widget(self->spec);
            }
        }
    }

    static void src_load_response(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        auto* self = static_cast<SpectrumViewer*>(w->parent_struct);
        if(user_data !=NULL) {
            self->src_file = *(const char**)user_data;
            self->src_label->label = self->src_file.data();
            expose_widget(self->src_label);
            if ( self->af.getAudioFile(self->src_file.c_str(), self->sampleRate) ) {
                self->srcf.clear();
                for (uint32_t i = 0; i < self->af.samplesize; i++) {
                    self->srcf.push_back((double)self->af.samples[i]);
                }
                self->ip->computeIR(self->dstf, self->srcf, self->sampleRate, self->irLength);
                self->setData(self->ip->getRefMag(), self->ip->getSrcMag(),
                                self->ip->getDiffMag(), self->ip->getIRMag());
                expose_widget(self->spec);
            }
        }
    }

    static void save_response(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        auto* self = static_cast<SpectrumViewer*>(w->parent_struct);
        if(user_data !=NULL) {
            self->ir_file = *(const char**)user_data;
            std::vector<double> ir = self->ip->createIR();
            self->af.saveAudioFile(self->ir_file, ir, ir.size(), self->sampleRate);
            std::cout << "save as: " << self->ir_file << std::endl;
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
    static void draw_window(void* w_, void* user_data) {
        Widget_t* w = (Widget_t*)w_;
        cairo_t* cr = w->crb;
        cairo_set_source_rgb(cr, 0.157, 0.165, 0.212);
        cairo_paint(cr);
    }

    static void draw_callback(void* w_, void* user_data) {
        Widget_t* w = (Widget_t*)w_;
        auto* self = static_cast<SpectrumViewer*>(w->parent_struct);
        self->draw(w_);
    }

    void draw(void* w_) {
        Widget_t* w = (Widget_t*)w_;

        Metrics_t m;
        os_get_window_metrics(w, &m);
        if (!m.visible) return;

        cairo_t* cr = w->crb;

        const int width  = m.width;
        const int height = m.height;

        const float sample_rate = sampleRate;

        cairo_set_source_rgb(cr, 0.188, 0.188, 0.188);
        cairo_rectangle(cr, 0, 0, width, height);
        cairo_fill(cr);

        cairo_set_source_rgba(cr, 0.267, 0.267, 0.267, 0.8);
        cairo_set_line_width(cr, 1.0);

        const float freqs[] = {20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000};

        for (float f : freqs) {
            float x = freq_to_x(f, f_min, f_max, width);
            cairo_move_to(cr, x, 0);
            cairo_line_to(cr, x, height);
        }
        cairo_stroke(cr);

        const float db_lines[] = {-96, -84, -72, -60, -48, -36, -24, -12, 0, 6};

        for (float d : db_lines) {
            float y = db_to_y(d, db_min, db_max, height);
            cairo_move_to(cr, 0, y);
            cairo_line_to(cr, width, y);
        }
        cairo_stroke(cr);

        drawSpectrum(cr, ref_,   width, height, sample_rate, 1.0, 0.333, 0.333,   "reference",  20);
        drawSpectrum(cr, source_,width, height, sample_rate, 0.314, 0.98, 0.482,  "source",     40);
        drawSpectrum(cr, diff_,  width, height, sample_rate, 1.0, 0.722, 0.424,   "diff", 60, true);
        drawSpectrum(cr, ir_,    width, height, sample_rate, 0.545, 0.914, 0.992, "impulse",    80);

        cairo_set_source_rgba(cr, 0.973, 0.973, 0.949, 0.6);
        cairo_set_font_size(cr, 10);

        for (float d : db_lines) {
            float y = db_to_y(d, db_min, db_max, height);
            char buf[16];
            snprintf(buf, sizeof(buf), "%.0f", d);
            draw_text(cr, 4, y - 2, buf);
        }

        for (float f : freqs) {
            float x = freq_to_x(f, f_min, f_max, width);
            char buf[16];

            if (f >= 1000.0f)
                snprintf(buf, sizeof(buf), "%.0fk", f / 1000.0f);
            else
                snprintf(buf, sizeof(buf), "%.0f", f);

            draw_text(cr, std::min<int>(width - 20, x - 10), height - 4, buf);
        }
    }

    void drawSpectrum(cairo_t* cr, const Vec& mags, int width, int height,
                      float sample_rate, float r, float g, float b,
                      const char* label, float label_y, bool dash = false) {

        if (mags.empty()) return;

        cairo_set_source_rgba(cr, r, g, b, 1.0);
        draw_text(cr, width - 60, label_y, label);

        cairo_set_line_width(cr, 1.5);
        static const double dashes[] = {2.0};
        if (dash) {
            cairo_set_dash(cr, dashes, 1, 0);
            cairo_set_line_width(cr, 1.0);
        } else {
            cairo_set_dash(cr, dashes, 0, 0);
        }
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

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
                cairo_move_to(cr, x, y);
                started = true;
            } else {
                if (x > last_x + 0.5f) {
                    cairo_line_to(cr, x, y);
                    last_x = x;
                }
            }
        }

        cairo_stroke(cr);
    }
};
