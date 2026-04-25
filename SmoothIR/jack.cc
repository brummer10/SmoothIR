/*
 * jack.cc
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2025 brummer <brummer@web.de>
 */

#include <jack/jack.h>
#include <jack/thread.h>
#include <jack/midiport.h>


/****************************************************************
        jack.cc   native jackd mono support
        
        this file is meant to be included in main.
****************************************************************/

jack_client_t *client;
jack_port_t *in_port;
//jack_port_t *midi_port;
jack_port_t *out_port;
bool runProcess = false;

void jack_shutdown (void *arg) {
    runProcess = false;
    fprintf (stderr, "jack shutdown, exit now \n");
    //r->quitGui();
}

int jack_xrun_callback(void *arg) {
    fprintf (stderr, "Xrun \r");
    return 0;
}

int jack_srate_callback(jack_nframes_t samplerate, void* arg) {
    int prio = jack_client_real_time_priority(client);
    if (prio < 0) prio = 25;
    fprintf (stderr, "Samplerate %iHz \n", samplerate);
    sw.setSampleRate(samplerate);
    //r->initEngine(samplerate, prio, 1);
    return 0;
}

int jack_buffersize_callback(jack_nframes_t nframes, void* arg) {
    fprintf (stderr, "Buffersize is %i samples \n", nframes);
    return 0;
}

void process_midi(void* midi_input_port_buf) {
    jack_midi_event_t in_event;
    jack_nframes_t event_count = jack_midi_get_event_count(midi_input_port_buf);
    unsigned int i;
    for (i = 0; i < event_count; i++) {
        jack_midi_event_get(&in_event, midi_input_port_buf, i);
        if ((in_event.buffer[0] & 0xf0) == 0xc0) {  // program change on any midi channel
            //fprintf(stderr,"program changed %i", (int)in_event.buffer[1]);
           // r->loadPreset((int)in_event.buffer[1]);
        } else if ((in_event.buffer[0] & 0xf0) == 0xb0) {   // controller
            if (in_event.buffer[1]== 120) { // engine mute by All Sound Off on any midi channel
                //fprintf(stderr,"mute %i", (int)in_event.buffer[2]);
            } else if ((in_event.buffer[1]== 32 ||
                        in_event.buffer[1]== 0)) { // bank change (LSB/MSB) on any midi channel
                //fprintf(stderr,"bank changed %i", (int)in_event.buffer[2]);
            } else {
               // fprintf(stderr,"controller changed %i value %i", (int)in_event.buffer[1], (int)in_event.buffer[2]);
            }
        } else if ((in_event.buffer[0] & 0xf0) == 0x90) {   // Note On
            //fprintf(stderr,"Note On %i", (int)in_event.buffer[1]);
        }
    }

}

int jack_process(jack_nframes_t nframes, void *arg) {
    if (!runProcess) return 0;
    //void *midi_in = jack_port_get_buffer (midi_port, nframes);
    float *input = static_cast<float *>(jack_port_get_buffer (in_port, nframes));
    float *output = static_cast<float *>(jack_port_get_buffer (out_port, nframes));
    
    if(output != input)
        memcpy(output, input, nframes*sizeof(float));
    //process_midi(midi_in);
    conv.process(nframes, input, output);

    return 0;
}

void startJack() {

    if ((client = jack_client_open ("smoothir", JackNoStartServer, NULL)) == 0) {
        fprintf (stderr, "jack server not running?\n");
        //r->quitGui();
    }

    if (client) {
       // midi_port = jack_port_register(
       //                client, "in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
        in_port = jack_port_register(
                       client, "in_0", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
        out_port = jack_port_register(
                       client, "out_0", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

        jack_set_xrun_callback(client, jack_xrun_callback, 0);
        jack_set_sample_rate_callback(client, jack_srate_callback, 0);
        jack_set_buffer_size_callback(client, jack_buffersize_callback, 0);
        jack_set_process_callback(client, jack_process, 0);
        jack_on_shutdown (client, jack_shutdown, 0);

        if (jack_activate (client)) {
            fprintf (stderr, "cannot activate client");
            //r->quitGui();
        }

        if (!jack_is_realtime(client)) {
            fprintf (stderr, "jack isn't running with realtime priority\n");
        } else {
            fprintf (stderr, "jack running with realtime priority\n");
        }
        //r->enableEngine(1);
        runProcess = true;
    }
}

void quitJack() {
    runProcess = false;
    if (client) {
       // if (jack_port_connected(midi_port)) {
       //     jack_port_disconnect(client,midi_port);
       // }
       // jack_port_unregister(client,midi_port);

        if (jack_port_connected(in_port)) {
            jack_port_disconnect(client,in_port);
        }
        jack_port_unregister(client,in_port);

        if (jack_port_connected(out_port)) {
            jack_port_disconnect(client,out_port);
        }
        jack_port_unregister(client,out_port);

        jack_client_close (client);
    }    
}
