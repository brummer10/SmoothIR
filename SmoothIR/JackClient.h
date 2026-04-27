
/*
 * JackClient.h
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2025 brummer <brummer@web.de>
 */

#pragma once

#include <jack/jack.h>
#include <jack/thread.h>

#include <cstdio>
#include <cstring>

#include "ParallelThread.h"

class IRMorpher;
class SpectrumViewer;

class JackClient {
public:
    JackClient(IRMorpher* conv_, SpectrumViewer* sw_, FFTAnalyzer* ana_) : xrworker()
{
        conv = conv_;
        sw = sw_;
        ana = ana_;
        abuffer = new float[8192];
        memset(abuffer, 0, 8192 * sizeof(float));
        xrworker.start();
    }

    ~JackClient() {
        stop();
        xrworker.stop();
        delete[] abuffer;
    }

    bool start(const char* name = "smoothir") {
        client = jack_client_open(name, JackNoStartServer, nullptr);

        if (!client) {
            fprintf(stderr, "jack server not running?\n");
            sw->quitGui();
            return false;
        }

        in_port = jack_port_register(
            client, "in_0", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);

        out_port = jack_port_register(
            client, "out_0", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

        jack_set_xrun_callback(client, xrunCallback, this);
        jack_set_sample_rate_callback(client, srateCallback, this);
        jack_set_buffer_size_callback(client, bufferSizeCallback, this);
        jack_set_process_callback(client, processCallback, this);
        jack_on_shutdown(client, shutdownCallback, this);

        if (jack_activate(client)) {
            fprintf(stderr, "cannot activate client\n");
            sw->quitGui();
            return false;
        }

        if (!jack_is_realtime(client)) {
            fprintf(stderr, "jack isn't running with realtime priority\n");
        } else {
            fprintf(stderr, "jack running with realtime priority\n");
        }

        runProcess = true;
        return true;
    }

    void stop() {
        runProcess = false;
        ana->cleanup();
        if (!client) return;

        if (in_port) {
            if (jack_port_connected(in_port)) {
                jack_port_disconnect(client, in_port);
            }
            jack_port_unregister(client, in_port);
            in_port = nullptr;
        }

        if (out_port) {
            if (jack_port_connected(out_port)) {
                jack_port_disconnect(client, out_port);
            }
            jack_port_unregister(client, out_port);
            out_port = nullptr;
        }

        jack_client_close(client);
        client = nullptr;
    }

private:
    ParallelThread     xrworker;
    IRMorpher* conv = nullptr;
    SpectrumViewer* sw = nullptr;
    FFTAnalyzer* ana = nullptr;
    jack_client_t* client = nullptr;
    jack_port_t* in_port = nullptr;
    jack_port_t* out_port = nullptr;
    bool runProcess = false;
    float *abuffer = nullptr;
    uint32_t frames = 0;

private:
    // -------- Static Callbacks --------

    static void shutdownCallback(void* arg) {
        auto* self = static_cast<JackClient*>(arg);
        if (!self) return;
        self->runProcess = false;
        fprintf(stderr, "jack shutdown, exit now\n");
        self->sw->quitGui();
    }

    static int xrunCallback(void* arg) {
        fprintf(stderr, "Xrun\r");
        return 0;
    }

    static int srateCallback(jack_nframes_t samplerate, void* arg) {
        auto* self = static_cast<JackClient*>(arg);
        if (!self) return 0;
        int prio = jack_client_real_time_priority(self->client);
        if (prio < 0) prio = 25;
        fprintf(stderr, "Samplerate %u Hz\n", samplerate);
        self->sw->setSampleRate((int)samplerate);
        self->ana->init(4096, (float)samplerate);
        self->xrworker.setThreadName("Worker");
        self->xrworker.set<JackClient, &JackClient::analyse>(self);
        self->xrworker.runProcess();
        return 0;
    }

    static int bufferSizeCallback(jack_nframes_t nframes, void* arg) {
        fprintf(stderr, "Buffersize is %u samples\n", nframes);
        return 0;
    }

    void analyse() {
        if (!frames) return;
        ana->processBlock(abuffer, frames);
    }

    static int processCallback(jack_nframes_t nframes, void* arg) {
        auto* self = static_cast<JackClient*>(arg);
        if (!self || !self->runProcess) return 0;

        float* input = static_cast<float*>(
            jack_port_get_buffer(self->in_port, nframes));

        float* output = static_cast<float*>(
            jack_port_get_buffer(self->out_port, nframes));

        if (output != input)
            memcpy(output, input, nframes * sizeof(float));

        self->conv->process(nframes, input, output);

        memcpy(self->abuffer, output, nframes * sizeof(float));
        self->frames = nframes;
        self->xrworker.runProcess();

        return 0;
    }
};
