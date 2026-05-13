
/*
 * AudioFile.h
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */


#include <iostream>
#include <cstring>
#include <new>
#include <sndfile.hh>
#include <vector>

#include "CheckResample.h"

#pragma once

#ifndef AUDIOFILE_H
#define AUDIOFILE_H

/****************************************************************
        class AudioFile - load a Audio File into buffer
                          and resample when needed
                          save a buffer to audio file
****************************************************************/

class AudioFile : public CheckResample {
public:
    uint32_t channels;
    uint32_t samplesize;
    uint32_t samplerate;
    double*   samplesL;
    double*   samplesR;
    double* saveBuffer;
    
    AudioFile() {
        channels   = 0;
        samplesize = 0;
        samplerate = 0;
        samplesL    = nullptr;
        samplesR    = nullptr;
        saveBuffer = nullptr;
    }
    
    ~AudioFile() {
        delete[] samplesL;
        delete[] samplesR;
        delete[] saveBuffer;
    }

    inline bool deinterleave() {
        delete[] samplesL;
        samplesL = nullptr;
        delete[] samplesR;
        samplesR = nullptr;
        uint32_t buffersize = channels == 2 ? samplesize/2 : samplesize;
        uint32_t c =  channels == 2 ? 1 : 0;
        try {
            samplesL = new double[buffersize];
            samplesR = new double[buffersize];
        } catch (...) {
            std::cerr << "Error: could not load file" << std::endl;
            return false;
        }
        std::memset(samplesL, 0, buffersize * sizeof(double));
        std::memset(samplesR, 0, buffersize * sizeof(double));
        for (uint32_t i = 0; i < buffersize; i++) {
            samplesL[i] = saveBuffer[i * channels] ;
            samplesR[i] = saveBuffer[i * channels + c] ;
        }
        delete[] saveBuffer;
        saveBuffer = nullptr;
        channels = 2;
        samplesize = buffersize;
        return true;
    }

    // load a Audio File into the buffer
    inline bool getAudioFile(const char* file, const uint32_t expectedSampleRate = 0) {
        SF_INFO info;
        info.format = 0;

        channels = 0;
        samplesize = 0;
        samplerate = 0;
        delete[] saveBuffer;
        saveBuffer = nullptr;
        // Open the wave file for reading
        SNDFILE *sndfile = sf_open(file, SFM_READ, &info);

        if (!sndfile) {
            std::cerr << "Error: could not open file " << sf_error (sndfile) << std::endl;
            return false;
        }
        if (info.channels > 2) {
            std::cerr << "Error: only two channels maximum are supported!" << std::endl;
            sf_close(sndfile);
            return false;
        }
        try {
            saveBuffer = new double[info.frames * info.channels];
        } catch (...) {
            std::cerr << "Error: could not load file" << std::endl;
            sf_close(sndfile);
            return false;
        }
        std::memset(saveBuffer, 0, info.frames * info.channels * sizeof(double));
        samplesize = (uint32_t) sf_readf_double(sndfile, &saveBuffer[0], info.frames);
        if (!samplesize ) samplesize = info.frames;
        channels = info.channels;
        samplerate = info.samplerate;
        sf_close(sndfile);
        if (expectedSampleRate)
            saveBuffer = checkSampleRate(&samplesize, channels, saveBuffer, samplerate, expectedSampleRate);
        return deinterleave();
    }

    // save a audio file from buffer to file
    void saveAudioFile(const std::string& name,
                       const std::vector<double>& bufferL,
                       const std::vector<double>& bufferR,
                       const uint32_t sampleRate) {

        const bool stereo = !bufferR.empty();

        const uint32_t frames = stereo ? std::min(bufferL.size(), bufferR.size()) : bufferL.size();

        if (frames == 0) {
            std::cerr << "Error: empty buffer\n";
            return;
        }

        SF_INFO sfinfo {};
        sfinfo.channels   = stereo ? 2 : 1;
        sfinfo.samplerate = sampleRate;
        sfinfo.format     = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

        SNDFILE* sf = sf_open(name.c_str(), SFM_WRITE, &sfinfo);

        if (!sf) {
            std::cerr << "Error: failed to open " << name << std::endl;
            return;
        }

        if (stereo) {
            std::vector<double> interleaved(frames * 2);
            for (uint32_t i = 0; i < frames; ++i) {
                interleaved[i * 2]     = bufferL[i];
                interleaved[i * 2 + 1] = bufferR[i];
            }
            sf_writef_double(sf, interleaved.data(), frames);
        } else {
            sf_writef_double(sf, bufferL.data(), frames);
        }

        sf_write_sync(sf);
        sf_close(sf);
    }

};

#endif
