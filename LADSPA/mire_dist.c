#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <ladspa.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define DIST_GAIN      0
#define DIST_MODE      1
#define DIST_OUT       2
#define DIST_HPF_ON    3
#define DIST_PEAK_F    4
#define DIST_PEAK_Q    5
#define DIST_PEAK_G    6
#define DIST_OVERSAMPLE 7
#define DIST_IN_L      8
#define DIST_IN_R      9
#define DIST_OUT_L     10
#define DIST_OUT_R     11

typedef struct {
    float a0, a1, a2, b1, b2;
    float x1, x2, y1, y2;
} Biquad;

typedef struct {
    LADSPA_Data * ports[12];
    float m_fSR;
    Biquad hpfL[2], hpfR[2]; 
    Biquad bellL, bellR;
    float last_xL, last_xR;
} MireDist;

void setup_bell(Biquad *f, float freq, float Q, float gainDB, float sr) {
    float A = powf(10.0f, gainDB / 40.0f);
    float omega = 2.0f * M_PI * freq / sr;
    float sn = sinf(omega), cs = cosf(omega);
    float alpha = sn / (2.0f * Q);
    float a0_inv = 1.0f / (1.0f + alpha / A);
    f->a0 = (1.0f + alpha * A) * a0_inv;
    f->a1 = (-2.0f * cs) * a0_inv;
    f->a2 = (1.0f - alpha * A) * a0_inv;
    f->b1 = (-2.0f * cs) * a0_inv;
    f->b2 = (1.0f - alpha / A) * a0_inv;
}

void setup_hpf_200(Biquad *f, float sr) {
    float omega = 2.0f * M_PI * 200.0f / sr;
    float sn = sinf(omega), cs = cosf(omega);
    float alpha = sn / (2.0f * 0.7071f);
    float a0_inv = 1.0f / (1.0f + alpha);
    f->a0 = ((1.0f + cs) * 0.5f) * a0_inv;
    f->a1 = -(1.0f + cs) * a0_inv; f->a2 = f->a0;
    f->b1 = (-2.0f * cs) * a0_inv; f->b2 = (1.0f - alpha) * a0_inv;
}

inline float process_biquad(Biquad *f, float in) {
    float out = f->a0 * in + f->a1 * f->x1 + f->a2 * f->x2 - f->b1 * f->y1 - f->b2 * f->y2;
    if (fabsf(out) < 1e-15f) out = 0.0f; 
    f->x2 = f->x1; f->x1 = in;
    f->y2 = f->y1; f->y1 = out;
    return out;
}

float apply_dist(float in, int mode) {
    switch(mode) {
        case 0: return (in > 1.0f) ? 1.0f : (in < -1.0f ? -1.0f : in);
        case 1: return tanhf(in);
        case 2: return sinf(in * M_PI * 0.5f);
        case 3: return (fabsf(in) * 2.0f) - 1.0f;
        case 4: return roundf(in * 8.0f) / 8.0f;
        default: return in;
    }
}

LADSPA_Handle instantiateDist(const LADSPA_Descriptor * Descriptor, unsigned long SampleRate) {
    MireDist * p = (MireDist *)calloc(1, sizeof(MireDist));
    if (p) {
        p->m_fSR = (float)SampleRate;
        setup_hpf_200(&p->hpfL[0], p->m_fSR);
        p->hpfL[1] = p->hpfL[0]; p->hpfR[0] = p->hpfL[0]; p->hpfR[1] = p->hpfL[0];
    }
    return (LADSPA_Handle)p;
}

void connectPortDist(LADSPA_Handle Instance, unsigned long Port, LADSPA_Data * Data) {
    ((MireDist *)Instance)->ports[Port] = Data;
}

void runDist(LADSPA_Handle Instance, unsigned long SampleCount) {
    MireDist * p = (MireDist *)Instance;

    
    float in_gain = powf(10.0f, *p->ports[DIST_GAIN] / 20.0f);
    float out_gain = powf(10.0f, *p->ports[DIST_OUT] / 20.0f);
    int mode = (int)(*p->ports[DIST_MODE] + 0.5f);
    int hpf_on = (*p->ports[DIST_HPF_ON] > 0.5f);
    
    int os_knob = (int)(*p->ports[DIST_OVERSAMPLE] + 0.5f);
    int os_factor = (os_knob == 1) ? 4 : (os_knob == 2 ? 8 : 2);
    float inv_os = 1.0f / (float)os_factor;

    
    setup_bell(&p->bellL, *p->ports[DIST_PEAK_F], *p->ports[DIST_PEAK_Q], *p->ports[DIST_PEAK_G], p->m_fSR);
    p->bellR = p->bellL;
    

    for (unsigned long i = 0; i < SampleCount; i++) {
        float sL = p->ports[DIST_IN_L][i];
        float sR = p->ports[DIST_IN_R][i];

        if (hpf_on) {
            sL = process_biquad(&p->hpfL[1], process_biquad(&p->hpfL[0], sL));
            sR = process_biquad(&p->hpfR[1], process_biquad(&p->hpfR[0], sR));
        }

        sL = process_biquad(&p->bellL, sL);
        sR = process_biquad(&p->bellR, sR);

        float dL = sL * in_gain;
        float dR = sR * in_gain;

        float accumL = 0.0f, accumR = 0.0f;
        
        float diffL = (dL - p->last_xL) * inv_os;
        float diffR = (dR - p->last_xR) * inv_os;
        float curL = p->last_xL;
        float curR = p->last_xR;

        for (int step = 0; step < os_factor; step++) {
            curL += diffL;
            curR += diffR;
            accumL += apply_dist(curL, mode);
            accumR += apply_dist(curR, mode);
        }
        
        p->last_xL = dL;
        p->last_xR = dR;

        p->ports[DIST_OUT_L][i] = (accumL * inv_os) * out_gain;
        p->ports[DIST_OUT_R][i] = (accumR * inv_os) * out_gain;
    }
}

void cleanupDist(LADSPA_Handle Instance) { free(Instance); }

static LADSPA_Descriptor * g_desc = NULL;
const LADSPA_Descriptor * ladspa_descriptor(unsigned long Index) {
    if (Index != 0) return NULL;
    if (!g_desc) {
        g_desc = (LADSPA_Descriptor *)calloc(1, sizeof(LADSPA_Descriptor));
        g_desc->UniqueID = 604005;
        g_desc->Label = strdup("mire_dist");
        g_desc->Name = strdup("Mire Dist");
        g_desc->Maker = strdup("Mire");
        g_desc->PortCount = 12;

        LADSPA_PortDescriptor * pd = (LADSPA_PortDescriptor *)calloc(12, sizeof(LADSPA_PortDescriptor));
        for(int i=0; i<8; i++) pd[i] = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        pd[8]=pd[9] = LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO;
        pd[10]=pd[11] = LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO;
        g_desc->PortDescriptors = pd;

        const char ** names = (const char **)calloc(12, sizeof(char *));
        names[0]="In Gain (dB)"; 
        names[1]="Mode (0-HC, 1-SC, 2-WF, 3-FW, 4-BC)"; 
        names[2]="Out Gain (dB)";
        names[3]="HPF 200Hz On"; 
        names[4]="Peak Cutoff"; 
        names[5]="Peak Q"; 
        names[6]="Peak Strength";
        names[7]="Oversample (0-2x, 1-4x, 2-8x)";
        names[8]="In L"; names[9]="In R"; names[10]="Out L"; names[11]="Out R";
        g_desc->PortNames = names;

        LADSPA_PortRangeHint * h = (LADSPA_PortRangeHint *)calloc(12, sizeof(LADSPA_PortRangeHint));
        h[0].LowerBound=0; h[0].UpperBound=60; h[0].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MIDDLE;
        h[1].LowerBound=0; h[1].UpperBound=4; h[1].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_INTEGER|LADSPA_HINT_DEFAULT_MINIMUM;
        h[2].LowerBound=-60; h[2].UpperBound=0; h[2].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MIDDLE;
        h[3].HintDescriptor=LADSPA_HINT_TOGGLED|LADSPA_HINT_DEFAULT_1;
        h[4].LowerBound=100; h[4].UpperBound=8000; h[4].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_LOGARITHMIC|LADSPA_HINT_DEFAULT_MIDDLE;
        h[5].LowerBound=0.1; h[5].UpperBound=10.0; h[5].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MIDDLE;
        h[6].LowerBound=0; h[6].UpperBound=30; h[6].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MINIMUM;
        h[7].LowerBound=0; h[7].UpperBound=2; h[7].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_INTEGER|LADSPA_HINT_DEFAULT_MIDDLE;
        
        g_desc->PortRangeHints = h;
        g_desc->instantiate = instantiateDist; g_desc->connect_port = connectPortDist; g_desc->run = runDist; g_desc->cleanup = cleanupDist;
    }
    return g_desc;
}
