/*
 * CheckResample.h
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2024 brummer <brummer@web.de>
 */

/****************************************************************
        CheckResample.h - resample buffer when needed
                          using cubic hermite interpolation
****************************************************************/

#pragma once

#include <cstdint>
#include <cmath>
#include <cstring>

class CheckResample {
public:
    CheckResample() {}

    double *checkSampleRate(uint32_t *count, uint32_t chan, double *input,
                           uint32_t fs_in, uint32_t fs_out) {
        if (fs_in == fs_out) return input;

        double ratio = double(fs_in) / double(fs_out);
        uint32_t outFrames = (uint32_t)std::ceil(*count / ratio);
        double *out = new double[outFrames * chan];

        for (uint32_t ch = 0; ch < chan; ++ch) {
            double srcPos = 0.0;

            for (uint32_t i = 0; i < outFrames; ++i) {
                uint32_t ip = (uint32_t)srcPos;
                double t = srcPos - ip;

                auto S = [&](int idx)->double {
                    if (idx < 0) return input[ch];
                    if ((uint32_t)idx >= *count)
                        return input[(*count - 1) * chan + ch];
                    return input[idx * chan + ch];
                };

                double x0 = S(ip - 1);
                double x1 = S(ip);
                double x2 = S(ip + 1);
                double x3 = S(ip + 2);

                out[i * chan + ch] = hermite(x0,x1,x2,x3,t);
                srcPos += ratio;
            }
        }

        delete[] input;
        *count = outFrames;
        return out;
    }

private:
    static inline double hermite(double x0, double x1, double x2, double x3, double t) {
        double c0 = x1;
        double c1 = 0.5f * (x2 - x0);
        double c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
        double c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
        return ((c3*t + c2)*t + c1)*t + c0;
    }
};

