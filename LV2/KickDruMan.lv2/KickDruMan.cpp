#include <lv2/core/lv2.h>
#include <lv2/atom/atom.h>
#include <lv2/atom/util.h>
#include <lv2/midi/midi.h>
#include <cmath>
#include <cstring>
#include <vector>

#define KICK_URI "https://goa.in.rs/KickDruMan"

typedef struct {
    float* f1; float* f2; float* f3;
    float* t1; float* c1; float* t2; float* c2;
    float* f3_vol; float* boxy_gain; float* boxy_q;
    const LV2_Atom_Sequence* midi_port;
    float* output;

    double samplerate;
    double timer;
    double phase;
    float envelope;
    float current_gain; // Track smoothed gain
    bool gate_active;
} KickDruMan;

static LV2_Handle instantiate(const LV2_Descriptor* descriptor, double samplerate, const char* bundle_path, const LV2_Feature* const* features) {
    KickDruMan* self = new KickDruMan();
    self->samplerate = samplerate;
    self->timer = 100.0;
    self->phase = 0.0;
    self->envelope = 0.0f;
    self->current_gain = 0.0f;
    self->gate_active = false;
    return (LV2_Handle)self;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
    KickDruMan* self = (KickDruMan*)instance;
    switch (port) {
        case 0: self->f1 = (float*)data; break;
        case 1: self->f2 = (float*)data; break;
        case 2: self->f3 = (float*)data; break;
        case 3: self->t1 = (float*)data; break;
        case 4: self->c1 = (float*)data; break;
        case 5: self->t2 = (float*)data; break;
        case 6: self->c2 = (float*)data; break;
        case 7: self->f3_vol = (float*)data; break;
        case 8: self->boxy_gain = (float*)data; break;
        case 9: self->boxy_q = (float*)data; break;
        case 10: self->midi_port = (const LV2_Atom_Sequence*)data; break;
        case 11: self->output = (float*)data; break;
    }
}

static void run(LV2_Handle instance, uint32_t n_samples) {
    KickDruMan* self = (KickDruMan*)instance;
    
    LV2_ATOM_SEQUENCE_FOREACH(self->midi_port, ev) {
        const uint8_t* const msg = (const uint8_t*)(ev + 1);
        uint8_t status = msg[0] & 0xF0;
        if (status == 0x90 && msg[2] > 0) {
            self->timer = 0.0;
            self->phase = 0.0;
            self->gate_active = true;
        } else if (status == 0x80 || (status == 0x90 && msg[2] == 0)) {
            self->gate_active = false;
        }
    }

    for (uint32_t i = 0; i < n_samples; ++i) {
        float t1_val = *self->t1; 
        float t2_val = *self->t2;
        float total_sweep_time = t1_val + t2_val;
        float sweep_freq = *self->f3;

        // 1. Frequency Sweep
        if (self->timer < total_sweep_time) {
            if (self->timer < t1_val) {
                float p1 = self->timer / t1_val;
                float cp1 = powf(p1, 1.0f - (*self->c1 * 0.9f));
                sweep_freq = *self->f1 + (*self->f2 - *self->f1) * cp1;
            } else {
                float p2 = (self->timer - t1_val) / t2_val;
                float cp2 = powf(p2, 1.0f - (*self->c2 * 0.9f));
                sweep_freq = *self->f2 + (*self->f3 - *self->f2) * cp2;
            }
        }

        // 2. Gain Smoothing Logic
        // Target is 1.0 during sweep, then f3_vol during sustain
        float target_gain = (self->timer < total_sweep_time) ? 1.0f : *self->f3_vol;
        // Glide current_gain toward target_gain (Low-pass filter at ~100Hz for smoothing)
        self->current_gain = 0.995f * self->current_gain + 0.005f * target_gain;

        // 3. Envelope (Note On/Off fade)
        if (self->gate_active) {
            self->envelope = 0.999f * self->envelope + 0.001f * 1.0f;
        } else {
            self->envelope *= 0.998f; // Exponential decay on release
            if (self->envelope < 0.0001f) self->envelope = 0.0f;
        }

        // 4. Oscillator
        self->phase += sweep_freq / self->samplerate;
        if (self->phase >= 1.0) self->phase -= 1.0;
        
        // Final output combinessmoothed gain AND the note envelope
        self->output[i] = sinf(2.0f * M_PI * self->phase) * self->current_gain * self->envelope;

        self->timer += (1.0 / self->samplerate);
    }
}

static void cleanup(LV2_Handle instance) { delete (KickDruMan*)instance; }

static const LV2_Descriptor descriptor = { KICK_URI, instantiate, connect_port, NULL, run, NULL, cleanup };
extern "C" LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index) { return (index == 0) ? &descriptor : NULL; }