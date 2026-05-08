
/*
 * Gain.h
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */

#include <cmath>


class Gain {
public:
    Gain() {};
    ~Gain() {};

    inline void clear_state()
    {
        for (int l0 = 0; (l0 < 2); l0 = (l0 + 1)) fRec3[l0] = 0.0;
        for (int l1 = 0; (l1 < 2); l1 = (l1 + 1)) fRec0[l1] = 0.0;
        for (int l2 = 0; (l2 < 2); l2 = (l2 + 1)) iRec1[l2] = 0;
        for (int l3 = 0; (l3 < 2); l3 = (l3 + 1)) fRec2[l3] = 0.0;
    }

    void setGain(float g) {
        gain = g;
    }

    const float getMeter() {
        return meter;
    }

    inline void init(uint32_t samplingFreq)
    {
        fSamplingFreq = samplingFreq;
        fConst0 = (1.0 / std::min<double>(192000.0, std::max<double>(1.0, double(fSamplingFreq))));
        gain = 0.0;
        power0 = 20.*log10(0.0000003); // -130db
        clear_state();
    }

    void process(int count, float *input0, float *output0) {
        double fSlow0 = (0.0010000000000000009 * std::pow(10.0, (0.050000000000000003 * double(gain))));
        for (int i = 0; (i < count); i = (i + 1)) {
            int iTemp0 = (iRec1[1] < 4096);
            fRec3[0] = (fSlow0 + (0.999 * fRec3[1]));
            double fTemp1 = (fRec3[0] * double(input0[i]));
            double fTemp2 = std::max<double>(fConst0, std::fabs(fTemp1));
            fRec0[0] = (iTemp0?std::max<double>(fRec0[1], fTemp2):fTemp2);
            iRec1[0] = (iTemp0?(iRec1[1] + 1):1);
            fRec2[0] = (iTemp0?fRec2[1]:fRec0[1]);
            power0 = fRec2[0];
            output0[i] = fTemp1;
            fRec3[1] = fRec3[0];
            fRec0[1] = fRec0[0];
            iRec1[1] = iRec1[0];
            fRec2[1] = fRec2[0];
        }
        meter = 20.*log10(std::max<double>(0.0000003,power0));
    }
private:
    uint32_t fSamplingFreq;
    double fConst0;
    double power0;
    double fRec3[2];
    double fRec0[2];
    double fRec2[2];
    int iRec1[2];
    float gain;
    float meter;
};
