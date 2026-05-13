
/*
 * Band.h
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */

#pragma once

struct Band {
    enum Type {
        LowShelf  = 0,
        Peak      = 1,
        HighShelf = 2
    };

    int enabled;
    Type type;
    double freq;
    double gain;
    double Q;
    int mute;
};
