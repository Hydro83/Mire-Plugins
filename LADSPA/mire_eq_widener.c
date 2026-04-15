#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <ladspa.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define WIDTH_IN_L    0
#define WIDTH_IN_R    1
#define WIDTH_OUT_L   2
#define WIDTH_OUT_R   3
#define BELL_BASE     4 
#define B_ON          0
#define B_FREQ        1
#define B_Q           2
#define B_GAIN        3

typedef struct {
    float b0, b1, b2, a1, a2;
    float z1, z2;
} BiQuad;

typedef struct {
    LADSPA_Data * ports[16];
    float m_fSR;
    BiQuad filtersL[3];
    BiQuad filtersR[3];
} MireWidth;

void update_filter(BiQuad *f, float freq, float q, float gainDb, float sr) {
    float A = powf(10.0f, gainDb / 40.0f);
    float omega = 2.0f * M_PI * freq / sr;
    float sn = sinf(omega);
    float cs = cosf(omega);
    float alpha = sn / (2.0f * q);

    float a0 = 1.0f + alpha / A;
    float a0_inv = 1.0f / a0;

    f->b0 = (1.0f + alpha * A) * a0_inv;
    f->b1 = (-2.0f * cs) * a0_inv;
    f->b2 = (1.0f - alpha * A) * a0_inv;
    f->a1 = (-2.0f * cs) * a0_inv;
    f->a2 = (1.0f - alpha / A) * a0_inv;
}

inline float process_filter(BiQuad *f, float in) {
    float out = f->b0 * in + f->z1;
    f->z1 = f->b1 * in - f->a1 * out + f->z2;
    f->z2 = f->b2 * in - f->a2 * out;
    
    if (fabsf(f->z1) < 1e-15f) f->z1 = 0.0f;
    if (fabsf(f->z2) < 1e-15f) f->z2 = 0.0f;
    
    return out;
}

LADSPA_Handle instantiateWidth(const LADSPA_Descriptor * Descriptor, unsigned long SampleRate) {
    MireWidth * p = (MireWidth *)calloc(1, sizeof(MireWidth));
    if (!p) return NULL;
    p->m_fSR = (float)SampleRate;
    return (LADSPA_Handle)p;
}

void connectPortWidth(LADSPA_Handle Instance, unsigned long Port, LADSPA_Data * Data) {
    ((MireWidth *)Instance)->ports[Port] = Data;
}

void runWidth(LADSPA_Handle Instance, unsigned long SampleCount) {
    MireWidth * p = (MireWidth *)Instance;

    
    int active[3];
    for (int i = 0; i < 3; i++) {
        int offset = BELL_BASE + (i * 4);
        active[i] = (*p->ports[offset + B_ON] > 0.5f);
        
        if (active[i]) {
            float freq = *p->ports[offset + B_FREQ];
            float q = *p->ports[offset + B_Q];
            float gain = *p->ports[offset + B_GAIN];
            
            update_filter(&p->filtersL[i], freq, q, gain, p->m_fSR);
            update_filter(&p->filtersR[i], freq, q, -gain, p->m_fSR);
        }
    }

    for (unsigned long i = 0; i < SampleCount; i++) {
        float outL = p->ports[WIDTH_IN_L][i];
        float outR = p->ports[WIDTH_IN_R][i];

        for (int j = 0; j < 3; j++) {
            if (active[j]) {
                outL = process_filter(&p->filtersL[j], outL);
                outR = process_filter(&p->filtersR[j], outR);
            }
        }
        
        p->ports[WIDTH_OUT_L][i] = outL;
        p->ports[WIDTH_OUT_R][i] = outR;
    }
}

void cleanupWidth(LADSPA_Handle Instance) {
    free(Instance);
}

static LADSPA_Descriptor * g_desc = NULL;
const LADSPA_Descriptor * ladspa_descriptor(unsigned long Index) {
    if (Index != 0) return NULL;
    if (!g_desc) {
        g_desc = (LADSPA_Descriptor *)calloc(1, sizeof(LADSPA_Descriptor));
        g_desc->UniqueID = 604007;
        g_desc->Label = strdup("mire_eq_widener");
        g_desc->Name = strdup("Mire EQ Widener");
        g_desc->Maker = strdup("Mire");
        g_desc->PortCount = 16;

        LADSPA_PortDescriptor * pd = (LADSPA_PortDescriptor *)calloc(16, sizeof(LADSPA_PortDescriptor));
        pd[0]=pd[1] = LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO;
        pd[2]=pd[3] = LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO;
        for(int i=4; i<16; i++) pd[i] = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        g_desc->PortDescriptors = pd;

        const char ** names = (const char **)calloc(16, sizeof(char *));
        names[0]="In L"; names[1]="In R"; names[2]="Out L"; names[3]="Out R";
        names[4]="B1: On"; names[5]="B1: Freq"; names[6]="B1: Q"; names[7]="B1: Depth";
        names[8]="B2: On"; names[9]="B2: Freq"; names[10]="B2: Q"; names[11]="B2: Depth";
        names[12]="B3: On"; names[13]="B3: Freq"; names[14]="B3: Q"; names[15]="B3: Depth";
        g_desc->PortNames = names;

        LADSPA_PortRangeHint * h = (LADSPA_PortRangeHint *)calloc(16, sizeof(LADSPA_PortRangeHint));

        // BAND 1
        h[4].HintDescriptor = LADSPA_HINT_TOGGLED | LADSPA_HINT_DEFAULT_1;
        h[5].LowerBound=100; h[5].UpperBound=10000; h[5].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MIDDLE;
        h[6].LowerBound=0.1; h[6].UpperBound=10; h[6].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MIDDLE;
        h[7].LowerBound=-24; h[7].UpperBound=24; h[7].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MAXIMUM;

        // BAND 2
        h[8].HintDescriptor = LADSPA_HINT_TOGGLED | LADSPA_HINT_DEFAULT_0;
        h[9].LowerBound=100; h[9].UpperBound=10000; h[9].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MIDDLE;
        h[10].LowerBound=0.1; h[10].UpperBound=10; h[10].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MIDDLE;
        h[11].LowerBound=-24; h[11].UpperBound=24; h[11].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MIDDLE;

        // BAND 3
        h[12].HintDescriptor = LADSPA_HINT_TOGGLED | LADSPA_HINT_DEFAULT_0;
        h[13].LowerBound=100; h[13].UpperBound=10000; h[13].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MIDDLE;
        h[14].LowerBound=0.1; h[14].UpperBound=10; h[14].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MIDDLE;
        h[15].LowerBound=-24; h[15].UpperBound=24; h[15].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MIDDLE;

        g_desc->PortRangeHints = h;

        g_desc->instantiate = instantiateWidth; 
        g_desc->connect_port = connectPortWidth; 
        g_desc->run = runWidth; 
        g_desc->cleanup = cleanupWidth;
    }
    return g_desc;
}
