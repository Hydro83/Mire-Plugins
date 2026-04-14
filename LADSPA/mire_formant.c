#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <ladspa.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MONK_VOWEL     0
#define MONK_SHARP     1
#define MONK_GAIN      2
#define MONK_IN_L      3
#define MONK_IN_R      4
#define MONK_OUT_L     5
#define MONK_OUT_R     6

typedef struct {
    float a0, a1, a2, b1, b2;
    float x1, x2, y1, y2;
} Biquad;

// Formant frequencies for A, E, I, O, U (approximate male monk style)
float formants[5][3] = {
    {600, 1040, 2250},  // A
    {400, 1620, 2400},  // E
    {250, 1750, 2600},  // I
    {400, 750,  2400},  // O
    {350, 600,  2400}   // U
};

typedef struct {
    LADSPA_Data * ports[7];
    float sr;
    Biquad filtersL[3], filtersR[3];
} MireMonk;

void setup_bandpass(Biquad *f, float freq, float Q, float sr) {
    float omega = 2.0f * M_PI * freq / sr;
    float sn = sinf(omega), cs = cosf(omega);
    float alpha = sn / (2.0f * Q);
    float a0_inv = 1.0f / (1.0f + alpha);
    f->a0 = alpha * a0_inv;
    f->a1 = 0;
    f->a2 = -alpha * a0_inv;
    f->b1 = -2.0f * cs * a0_inv;
    f->b2 = (1.0f - alpha) * a0_inv;
}

inline float process_biquad(Biquad *f, float in) {
    float out = f->a0 * in + f->a1 * f->x1 + f->a2 * f->x2 - f->b1 * f->y1 - f->b2 * f->y2;
    f->x2 = f->x1; f->x1 = in;
    f->y2 = f->y1; f->y1 = out;
    return out;
}

LADSPA_Handle instantiateMonk(const LADSPA_Descriptor * Descriptor, unsigned long SampleRate) {
    MireMonk * p = (MireMonk *)calloc(1, sizeof(MireMonk));
    p->sr = (float)SampleRate;
    return (LADSPA_Handle)p;
}

void connectPortMonk(LADSPA_Handle Instance, unsigned long Port, LADSPA_Data * Data) {
    ((MireMonk *)Instance)->ports[Port] = Data;
}

void runMonk(LADSPA_Handle Instance, unsigned long SampleCount) {
    MireMonk * p = (MireMonk *)Instance;
    float vowel_ptr = *p->ports[MONK_VOWEL]; // 0.0 to 4.0
    float sharpness = *p->ports[MONK_SHARP]; // Q factor
    float gain = powf(10.0f, *p->ports[MONK_GAIN] / 20.0f);

    // Vowel Interpolation Logic
    int v1 = (int)floorf(vowel_ptr);
    int v2 = (v1 + 1 > 4) ? 4 : v1 + 1;
    float blend = vowel_ptr - (float)v1;

    // Update the 3 parallel bandpass filters
    for (int i = 0; i < 3; i++) {
        float freq = formants[v1][i] + blend * (formants[v2][i] - formants[v1][i]);
        setup_bandpass(&p->filtersL[i], freq, sharpness, p->sr);
        p->filtersR[i] = p->filtersL[i];
    }

    for (unsigned long i = 0; i < SampleCount; i++) {
        float inL = p->ports[MONK_IN_L][i];
        float inR = p->ports[MONK_IN_R][i];
        float outL = 0, outR = 0;

        // Process filters in parallel (summed)
        for (int j = 0; j < 3; j++) {
            outL += process_biquad(&p->filtersL[j], inL);
            outR += process_biquad(&p->filtersR[j], inR);
        }

        p->ports[MONK_OUT_L][i] = outL * gain;
        p->ports[MONK_OUT_R][i] = outR * gain;
    }
}

void cleanupMonk(LADSPA_Handle Instance) { free(Instance); }

static LADSPA_Descriptor * g_desc = NULL;
const LADSPA_Descriptor * ladspa_descriptor(unsigned long Index) {
    if (Index != 0) return NULL;
    if (!g_desc) {
        g_desc = (LADSPA_Descriptor *)calloc(1, sizeof(LADSPA_Descriptor));
        g_desc->UniqueID = 604011;
        g_desc->Label = strdup("mire_formant");
        g_desc->Name = strdup("Mire Formant");
        g_desc->Maker = strdup("Mire");
        g_desc->PortCount = 7;

        LADSPA_PortDescriptor * pd = (LADSPA_PortDescriptor *)calloc(7, sizeof(LADSPA_PortDescriptor));
        for(int i=0; i<3; i++) pd[i] = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        pd[3]=pd[4] = LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO;
        pd[5]=pd[6] = LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO;
        g_desc->PortDescriptors = pd;

        const char ** names = (const char **)calloc(7, sizeof(char *));
        names[0]="Vowel (A-E-I-O-U)"; names[1]="Sharpness (Q)"; names[2]="Output Gain (dB)";
        names[3]="In L"; names[4]="In R"; names[5]="Out L"; names[6]="Out R";
        g_desc->PortNames = names;

        LADSPA_PortRangeHint * h = (LADSPA_PortRangeHint *)calloc(7, sizeof(LADSPA_PortRangeHint));
        h[0].LowerBound=0; h[0].UpperBound=4; h[0].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MIDDLE;
        h[1].LowerBound=2.0; h[1].UpperBound=20.0; h[1].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MIDDLE;
        h[2].LowerBound=-60; h[2].UpperBound=20; h[2].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_0;
        g_desc->PortRangeHints = h;

        g_desc->instantiate = instantiateMonk; g_desc->connect_port = connectPortMonk; g_desc->run = runMonk; g_desc->cleanup = cleanupMonk;
    }
    return g_desc;
}
