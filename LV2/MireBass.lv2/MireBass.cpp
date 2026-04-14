#include <lv2/core/lv2.h>
#include <lv2/atom/atom.h>
#include <lv2/atom/util.h>
#include <lv2/midi/midi.h>
#include <cmath>
#include <algorithm>

#define BASS_URI "https://goa.in.rs/MireBass"

struct SurgeStyleFilter {
    float s1; 
    SurgeStyleFilter() : s1(0.0f) {}
    inline float process(float input, float g) {
        float v = (input - s1) * (g / (1.0f + g));
        float res_out = v + s1;
        s1 = res_out + v;
        return res_out;
    }
};

float poly_blep(float t, float dt) {
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    else if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

typedef struct {
    float *wave_select, *f1, *f2, *f3, *t1, *c1, *t2, *c2, *res, *volume;
    const LV2_Atom_Sequence* midi_port;
    float* output;

    double samplerate, phase, timer;
    float freq, env_level;
    bool gate;

    SurgeStyleFilter stages[4];
    float last_out; 
} MireBass;

static LV2_Handle instantiate(const LV2_Descriptor* descriptor, double samplerate, const char* bundle_path, const LV2_Feature* const* features) {
    MireBass* self = new MireBass();
    self->samplerate = samplerate;
    self->phase = 0.0; self->timer = 100.0;
    self->env_level = 0.0f; self->gate = false;
    self->last_out = 0.0f;
    return (LV2_Handle)self;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
    MireBass* self = (MireBass*)instance;
    switch (port) {
        case 0:  self->wave_select = (float*)data; break;
        case 1:  self->f1 = (float*)data; break;
        case 2:  self->f2 = (float*)data; break;
        case 3:  self->f3 = (float*)data; break;
        case 4:  self->t1 = (float*)data; break;
        case 5:  self->c1 = (float*)data; break;
        case 6:  self->t2 = (float*)data; break;
        case 7:  self->c2 = (float*)data; break;
        case 8:  self->res = (float*)data; break;
        case 9:  self->volume = (float*)data; break;
        case 10: self->midi_port = (const LV2_Atom_Sequence*)data; break;
        case 11: self->output = (float*)data; break;
    }
}

static void run(LV2_Handle instance, uint32_t n_samples) {
    MireBass* self = (MireBass*)instance;

    LV2_ATOM_SEQUENCE_FOREACH(self->midi_port, ev) {
        const uint8_t* const msg = (const uint8_t*)(ev + 1);
        uint8_t status = msg[0] & 0xF0;
        if (status == 0x90 && msg[2] > 0) {
            self->freq = 440.0f * powf(2.0f, (msg[1] - 69.0f) / 12.0f);
            self->phase = 0.0; 
            self->timer = 0.0;
            for(int s=0; s<4; s++) self->stages[s].s1 = 0.0f;
            self->last_out = 0.0f;
            self->gate = true;
            self->env_level = 1.0f; 
        } else if (status == 0x80 || (status == 0x90 && msg[2] == 0)) {
            self->gate = false;
        }
    }

    float dt = self->freq / self->samplerate;
    bool is_square = (*self->wave_select > 0.5f);
    float res_amount = (*self->res) * 3.8f; 

    // Pre-calculate timing in seconds
    float t1_sec = *self->t1 * 0.001f;
    float t2_sec = *self->t2 * 0.001f;

    for (uint32_t i = 0; i < n_samples; ++i) {
        float sweep_cutoff = *self->f3;
        if (self->timer < (t1_sec + t2_sec)) {
            if (self->timer < t1_sec) {
                float p = self->timer / t1_sec;
                sweep_cutoff = *self->f1 + (*self->f2 - *self->f1) * powf(p, 1.0f - (*self->c1 * 0.9f));
            } else {
                float p = (self->timer - t1_sec) / t2_sec;
                sweep_cutoff = *self->f2 + (*self->f3 - *self->f2) * powf(p, 1.0f - (*self->c2 * 0.9f));
            }
        }
        sweep_cutoff = std::min(sweep_cutoff, (float)self->samplerate * 0.45f);

        float sig = is_square ? ((self->phase < 0.5f ? 1.0f : -1.0f) + poly_blep(self->phase, dt) - poly_blep(fmod(self->phase + 0.5f, 1.0f), dt)) 
                              : ((2.0f * self->phase) - 1.0f - poly_blep(self->phase, dt));

        float g = tanf(M_PI * sweep_cutoff / self->samplerate);
        float filter_in = sig - (res_amount * self->last_out);
        filter_in *= (1.0f + res_amount * 0.25f);

        float current_stage_out = filter_in;
        for(int s = 0; s < 4; s++) {
            current_stage_out = self->stages[s].process(current_stage_out, g);
        }
        
        self->last_out = current_stage_out;

        if (!self->gate) { 
            self->env_level *= 0.998f; 
            if (self->env_level < 0.0001f) self->env_level = 0.0f; 
        }

        self->output[i] = self->last_out * self->env_level * (*self->volume);
        self->phase = fmod(self->phase + dt, 1.0f);
        self->timer += (1.0 / self->samplerate);
    }
}

static void cleanup(LV2_Handle instance) { delete (MireBass*)instance; }
static const LV2_Descriptor descriptor = { BASS_URI, instantiate, connect_port, NULL, run, NULL, cleanup };
extern "C" LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index) { return (index == 0) ? &descriptor : NULL; }