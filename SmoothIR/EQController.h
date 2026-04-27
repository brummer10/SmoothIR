
/*
 * EQController.cc
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */

#include "xwidgets.h"


static void show_label(Widget_t *w, int width, int height) {
    //use_text_color_scheme(w, get_color_state(w));
    cairo_set_source_rgba(w->crb, 0.61, 0.649, 0.583, 1.0);
    cairo_text_extents_t extents;
    /** show label below the knob**/
    cairo_set_font_size (w->crb, w->app->normal_font/w->scale.ascale);
    cairo_text_extents(w->crb,w->label , &extents);
    cairo_move_to (w->crb, (width*0.5)-(extents.width/2), height-(extents.height/4));
    cairo_show_text(w->crb, w->label);
    cairo_new_path (w->crb);
}


void draw_my_knob(void *w_, void* user_data) {
    Widget_t *w = (Widget_t*)w_;
    Metrics_t metrics;
    os_get_window_metrics(w, &metrics);
    int width = metrics.width-2;
    int height = metrics.height - (w->app->small_font + 9 + w->app->normal_font);
    if (!metrics.visible) return;

    const double scale_zero = 20 * (M_PI/180); // defines "dead zone" for knobs
    int arc_offset = 2;
    int knob_x = 0;
    int knob_y = 0;

    int grow = (width > height) ? height:width;
    knob_x = grow-1;
    knob_y = grow-1;
    /** get values for the knob **/
    const int iw = width * 0.15;

    const int knobx1 = width* 0.5;

    const int knoby1 = height * 0.5;

    const double knobstate = adj_get_state(w->adj_y);
    const double angle = scale_zero + knobstate * 2 * (M_PI - scale_zero);

    const double pointer_off =knob_x/6;
    const double radius = min(knob_x-pointer_off, knob_y-pointer_off) / 2;

    const double add_angle = 90 * (M_PI / 180.);

    // base
    use_base_color_scheme(w, INSENSITIVE_);
    cairo_set_line_width(w->crb, iw/w->scale.ascale);
    cairo_arc (w->crb, knobx1, knoby1+arc_offset, radius,
          add_angle + scale_zero, add_angle + scale_zero + 320 * (M_PI/180));
    cairo_stroke(w->crb);

    // indicator
    cairo_new_sub_path(w->crb);
    use_fg_color_scheme(w, NORMAL_);
    cairo_arc (w->crb,knobx1, knoby1+arc_offset, radius,
          add_angle + scale_zero, add_angle + angle);
    cairo_stroke(w->crb);


    use_text_color_scheme(w, get_color_state(w));
    cairo_text_extents_t extents;
    /** show value below the kob**/
    char s[64];
    const char* format[] = {"%.1f %s", "%.2f %s", "%.3f %s"};
    float value = adj_get_value(w->adj);
    if (fabs(value)<10.0) {
        snprintf(s, 63, format[2-1], value, w->input_label);
    } else if (fabs(value)<100.0) {
        snprintf(s, 63, format[1-1], value, w->input_label);
    } else {
        snprintf(s, 63,"%d%s",  (int) value, w->input_label);
    }
    cairo_set_font_size (w->crb, w->app->small_font/w->scale.ascale);
    cairo_text_extents(w->crb, s, &extents);
    cairo_move_to (w->crb, knobx1-extents.width/2, height + (w->app->small_font)+extents.height/2);
    cairo_show_text(w->crb, s);
    cairo_new_path (w->crb);

    show_label(w, width, height + (w->app->small_font + 9) + w->app->normal_font);
}


Widget_t* add_my_knob(Widget_t *parent, const char * label, const char* type,
                int x, int y, int width, int height) {

    Widget_t *wid = add_knob(parent, label, x, y, width, height);
    wid->flags = USE_TRANSPARENCY | FAST_REDRAW;
    set_widget_color(wid, (Color_state)0, (Color_mod)0, 0.15, 0.52, 0.55, 1.0);
    wid->func.expose_callback = draw_my_knob;
    snprintf(wid->input_label, 31, "%s", type);
    return wid;
}


static void setFrameColour(Widget_t* w, cairo_t *cr, int x, int y, int wi, int h) {
    Colors *c = get_color_scheme(w, NORMAL_);
   // Colors *c1 = get_color_scheme(w, PRELIGHT_);
    cairo_pattern_t *pat = cairo_pattern_create_linear (x, y, x, y + h);
    cairo_pattern_add_color_stop_rgba
        (pat, 0, c->bg[0]*2.5, c->bg[1]*2.5, c->bg[2]*2.5,1.0);
    cairo_pattern_add_color_stop_rgba 
        (pat, 1, c->bg[0]*2.1, c->bg[1]*2.1, c->bg[2]*2.1,1.0);
    cairo_set_source(cr, pat);
    cairo_pattern_destroy (pat);
}

static void roundrec(cairo_t *cr, float x, float y, float width, float height, float r) {
    cairo_arc(cr, x+r, y+r, r, M_PI, 3*M_PI/2);
    cairo_arc(cr, x+width-r, y+r, r, 3*M_PI/2, 0);
    cairo_arc(cr, x+width-r, y+height-r, r, 0, M_PI/2);
    cairo_arc(cr, x+r, y+height-r, r, M_PI/2, M_PI);
    cairo_close_path(cr);
}


static void draw_frame(void *w_, void* user_data) {
    Widget_t *w = (Widget_t*)w_;
    Metrics_t metrics;
    os_get_window_metrics(w, &metrics);
    if (!metrics.visible) return;
    int width_t = metrics.width;
    int height_t = metrics.height;

    cairo_set_line_width(w->crb,2);
    cairo_set_source_rgba(w->crb, 0.16,0.16,0.18,1.0);
    roundrec(w->crb, 2, 0, width_t-4, height_t, 5);
    cairo_fill_preserve(w->crb);
/*
    cairo_pattern_t *pat = cairo_pattern_create_linear( 0, 0, 0, height_t);
    cairo_pattern_add_color_stop_rgba(pat, 0.00, 0.32, 0.36, 0.36, 0.83);
    cairo_pattern_add_color_stop_rgba(pat, 0.09, 0.25, 0.25, 0.25, 0.0);
    cairo_pattern_add_color_stop_rgba(pat, 0.92, 0.113, 0.113, 0.113, 0.0);
    cairo_pattern_add_color_stop_rgba(pat, 1.00, 0.083, 0.083, 0.083, 0.83);
    cairo_set_source(w->crb, pat);
    cairo_fill_preserve(w->crb);
    cairo_pattern_destroy(pat);
*/
    setFrameColour(w, w->crb, 5, 5, width_t-10, height_t-10);
    cairo_stroke(w->crb);
    cairo_new_path (w->crb);

}

Widget_t* add_my_frame(Widget_t *parent, const char * label,
                int x, int y, int width, int height) {

    Widget_t *wid = create_widget(parent->app, parent, x, y, width, height);
    wid->label = label;
    wid->scale.gravity = ASPECT;
    wid->func.expose_callback = draw_frame;
    return wid;
}

void draw_enable_button(void *w_, void* user_data) {
    Widget_t *w = (Widget_t*)w_;
    if (!w) return;

    Metrics_t metrics;
    os_get_window_metrics(w, &metrics);
    if (!metrics.visible) return;

    const int width  = metrics.width;
    const int height = metrics.height;
    const int state  = (int)adj_get_value(w->adj); // 0 = off, 1 = on
    const float cx = width * 0.5f;
    const float cy = height * 0.5f;
    const float r  = (width < height ? width : height) * 0.35f;

    roundrec(w->crb, 0, 0, width, height, 0.25);
    cairo_set_source_rgba(w->crb, 0.08, 0.08, 0.08, 1.0);
    cairo_fill(w->crb);

    Colors *c = get_color_scheme(w, NORMAL_);
    float cr = c->fg[0], cg = c->fg[1], cb = c->fg[2]; 
    float alpha = state ? 1.0 : 0.25;

    cairo_arc(w->crb, cx, cy, r, 0, 2 * M_PI);
    cairo_set_line_width(w->crb, 2.0);

    if (w->state == 1) { // hover
        cairo_set_source_rgba(w->crb, cr, cg, cb, alpha + 0.2);
        cairo_set_line_width(w->crb, 2.5);
    } else {
        cairo_set_source_rgba(w->crb, cr, cg, cb, alpha);
    }

    cairo_stroke(w->crb);
    float inner_r = r * 0.45f;
    cairo_arc(w->crb, cx, cy, inner_r, 0, 2 * M_PI);
    cairo_pattern_t* pat = cairo_pattern_create_radial(cx, cy, inner_r * 0.1, cx, cy, inner_r);

    if (state) {
        cairo_pattern_add_color_stop_rgba(pat, 0.0, cr, cg, cb, 1.0);
        cairo_pattern_add_color_stop_rgba(pat, 0.6, cr*0.6, cg*0.6, cb*0.6, 1.0);
        cairo_pattern_add_color_stop_rgba(pat, 1.0, 0.05, 0.05, 0.05, 1.0);
    } else {
         cairo_pattern_add_color_stop_rgba(pat, 0.0, 0.15, 0.15, 0.15, 1.0);
        cairo_pattern_add_color_stop_rgba(pat, 1.0, 0.03, 0.03, 0.03, 1.0);
    }

    cairo_set_source(w->crb, pat);
    cairo_fill(w->crb);
    cairo_pattern_destroy(pat);

    if (w->state == 1 && state) {
        cairo_arc(w->crb, cx, cy, r + 2.0, 0, 2 * M_PI);
        cairo_set_source_rgba(w->crb, cr, cg, cb, 0.15);
        cairo_set_line_width(w->crb, 3.0);
        cairo_stroke(w->crb);
    }

    cairo_new_path(w->crb);
}

void draw_power_button(void *w_, void* user_data) {
    Widget_t *w = (Widget_t*)w_;
    if (!w) return;

    Metrics_t metrics;
    os_get_window_metrics(w, &metrics);
    if (!metrics.visible) return;

    const int width  = metrics.width-2;
    const int height = metrics.height;
    const int state  = (int)adj_get_value(w->adj); // 0 = off, 1 = on

    float offset = 0.0f;
    if (w->state == 2) offset = 1.0f; // pressed

    const float cx = width * 0.6f + offset;
    const float cy = height * 0.5f + offset;
    const float r  = (width < height ? width : height) * 0.35f;

    Colors *c = get_color_scheme(w, NORMAL_);
    
    float cr = c->fg[0], cg = c->fg[1], cb = c->fg[2];

    float off = 0.35f;

    if (!state) {
        cr *= off;
        cg *= off;
        cb *= off;
        
    }

    float alpha = 1.0f;
    if (w->state == 1) alpha = 1.2f;
    float start_angle = -M_PI * 0.4;
    float end_angle   =  M_PI * 1.4;

    cairo_arc(w->crb, cx, cy, r, start_angle, end_angle);
    cairo_set_line_width(w->crb, 2.2);
    cairo_set_source_rgba(w->crb, cr * alpha, cg * alpha, cb * alpha, 1.0);
    cairo_stroke(w->crb);

    float line_len = r * 0.9f;
    cairo_move_to(w->crb, cx, cy - line_len);
    cairo_line_to(w->crb, cx, cy - r * 0.1f);
    cairo_set_line_width(w->crb, 2.2);
    cairo_stroke(w->crb);

    if (state) {
        cairo_arc(w->crb, cx, cy, r + 1.5f, 0, 2 * M_PI);
        cairo_set_source_rgba(w->crb, cr, cg, cb, 0.15);
        cairo_set_line_width(w->crb, 3.0);
        cairo_stroke(w->crb);
    }

    cairo_new_path(w->crb);
}

Widget_t *add_my_enable_button(Widget_t *parent, int x, int y, int width, int height, const char *label) {
    Widget_t *fbutton = add_toggle_button(parent, label, x, y, width, height);
    fbutton->scale.gravity = ASPECT;
    fbutton->func.expose_callback = draw_power_button;
    return fbutton;
}



void draw_combobox_button(void *w_, void* user_data) {
    Widget_t *w = (Widget_t*)w_;
    if (!w) return;
    Metrics_t metrics;
    os_get_window_metrics(w, &metrics);
    int width = metrics.width-2;
    int height = metrics.height-2;
    if (!metrics.visible) return;
    if (!w->state && (int)w->adj_y->value)
        w->state = 3;

    if(w->state==0) {
        cairo_set_line_width(w->crb, 1.0);
         use_frame_color_scheme(w, PRELIGHT_);
    } else if(w->state==1) {
        cairo_set_line_width(w->crb, 1.5);
        use_frame_color_scheme(w, PRELIGHT_);
    } else if(w->state==2) {
        cairo_set_line_width(w->crb, 1.0);
        use_frame_color_scheme(w, PRELIGHT_);
    } else if(w->state==3) {
        cairo_set_line_width(w->crb, 1.0);
        use_frame_color_scheme(w, PRELIGHT_);
    }
    cairo_stroke(w->crb); 

    float offset = 0.0;
    if(w->state==0) {
        use_fg_color_scheme(w, NORMAL_);
    } else if(w->state==1) {
        use_fg_color_scheme(w, PRELIGHT_);
        offset = 1.0;
    } else if(w->state==2) {
        use_fg_color_scheme(w, SELECTED_);
        offset = 2.0;
    } else if(w->state==3) {
        use_fg_color_scheme(w, ACTIVE_);
        offset = 1.0;
    }
    use_text_color_scheme(w, get_color_state(w));
    int wa = width/1.1;
    int h = height/2.2;
    int wa1 = width/1.55;
    int h1 = height/1.3;
    int wa2 = width/2.8;
   
    cairo_move_to(w->crb, wa+offset, h+offset);
    cairo_line_to(w->crb, wa1+offset, h1+offset);
    cairo_line_to(w->crb, wa2+offset, h+offset);
    cairo_line_to(w->crb, wa+offset, h+offset);
    cairo_fill(w->crb);
   
}


Widget_t* add_type_combobox(Widget_t *p,const char * label,
                                int x, int y, int width, int height) {
    Widget_t* w = add_combobox(p, label, x, y, width, height);
    w->childlist->childs[0]->func.expose_callback = draw_combobox_button;
    return w;
}
