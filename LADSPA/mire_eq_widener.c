#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <ladspa.h>

#define WIDTH_IN_L    0
#define WIDTH_IN_R    1
#define WIDTH_OUT_L   2
#define WIDTH_OUT_R   3

// Port offsets for the 3 Bells
#define BELL_BASE     4 
#define B_ON          0
#define B_FREQ        1
#define B_Q           2
#define B_GAIN        3

typedef struct {
    float x1, x2, y1, y2;
} BiQuad;

typedef struct {
    LADSPA_Data * ports[28];
    float m_fSR;
    BiQuad filtersL[3];
    BiQuad filtersR[3];
} MireWidth;

// Standard BiQuad Peak/Bell formula
void update_filter(BiQuad *f, float freq, float q, float gainDb, float sr) {
    float A = powf(10.0f, gainDb / 40.0f);
    float omega = 2.0f * M_PI * freq / sr;
    float sn = sinf(omega);
    float cs = cosf(omega);
    float alpha = sn / (2.0f * q);

    float b0 = 1.0f + alpha * A;
    float b1 = -2.0f * cs;
    float b2 = 1.0f - alpha * A;
    float a0 = 1.0f + alpha / A;
    float a1 = -2.0f * cs;
    float a2 = 1.0f - alpha / A;

    // Normalize coefficients
    f->x1 = b1 / a0; f->x2 = b2 / a0;
    f->y1 = a1 / a0; f->y2 = a2 / a0;
    // We repurpose x1/x2 for coefficients and use internal state elsewhere
}

// Internal processing for the filter
float process_filter(BiQuad *f, float in, float *z1, float *z2, float b0_norm) {
    float out = b0_norm * in + *z1;
    *z1 = f->x1 * in - f->y1 * out + *z2;
    *z2 = f->x2 * in - f->y2 * out;
    return out;
}

LADSPA_Handle instantiateWidth(const LADSPA_Descriptor * Descriptor, unsigned long SampleRate) {
    MireWidth * p = (MireWidth *)calloc(1, sizeof(MireWidth));
    p->m_fSR = (float)SampleRate;
    return (LADSPA_Handle)p;
}

void connectPortWidth(LADSPA_Handle Instance, unsigned long Port, LADSPA_Data * Data) {
    ((MireWidth *)Instance)->ports[Port] = Data;
}

void runWidth(LADSPA_Handle Instance, unsigned long SampleCount) {
    MireWidth * p = (MireWidth *)Instance;
    static float zL[3][2] = {0}, zR[3][2] = {0};

    // Update filters based on current knob positions
    for (int i = 0; i < 3; i++) {
        int offset = BELL_BASE + (i * 4);
        float freq = *p->ports[offset + B_FREQ];
        float q = *p->ports[offset + B_Q];
        float gain = *p->ports[offset + B_GAIN];
        
        // Left gets the Gain, Right gets -Gain (Mirror)
        update_filter(&p->filtersL[i], freq, q, gain, p->m_fSR);
        update_filter(&p->filtersR[i], freq, q, -gain, p->m_fSR);
    }

    for (unsigned long i = 0; i < SampleCount; i++) {
        float outL = p->ports[WIDTH_IN_L][i];
        float outR = p->ports[WIDTH_IN_R][i];

        for (int j = 0; j < 3; j++) {
            int offset = BELL_BASE + (j * 4);
            if (*p->ports[offset + B_ON] > 0.5f) {
                // Simplified Direct Form II calculation
                float A = powf(10.0f, (j==0? *p->ports[BELL_BASE+3] : (j==1? *p->ports[BELL_BASE+7] : *p->ports[BELL_BASE+11])) / 40.0f);
                float omega = 2.0f * M_PI * (*p->ports[offset+B_FREQ]) / p->m_fSR;
                float alpha = sinf(omega) / (2.0f * (*p->ports[offset+B_Q]));
                float b0L = (1.0f + alpha * A) / (1.0f + alpha / A);
                float b0R = (1.0f + alpha / A) / (1.0f + alpha * A);

                outL = process_filter(&p->filtersL[j], outL, &zL[j][0], &zL[j][1], b0L);
                outR = process_filter(&p->filtersR[j], outR, &zR[j][0], &zR[j][1], b0R);
            }
        }
        p->ports[WIDTH_OUT_L][i] = outL;
        p->ports[WIDTH_OUT_R][i] = outR;
    }
}

void cleanupWidth(LADSPA_Handle Instance) { free(Instance); }

static LADSPA_Descriptor * g_desc = NULL;
const LADSPA_Descriptor * ladspa_descriptor(unsigned long Index) {
    if (Index != 0) return NULL;
    if (!g_desc) {
        g_desc = (LADSPA_Descriptor *)calloc(1, sizeof(LADSPA_Descriptor));
        g_desc->UniqueID = 604010;
        g_desc->Label = strdup("mire_eq_widener");
        g_desc->Name = strdup("Mire EQ Widener");
        g_desc->Maker = strdup("Mire");
        g_desc->PortCount = 16; 

        LADSPA_PortDescriptor * pd = (LADSPA_PortDescriptor *)calloc(16, sizeof(LADSPA_PortDescriptor));
        pd[0]=pd[1]=LADSPA_PORT_INPUT|LADSPA_PORT_AUDIO;
        pd[2]=pd[3]=LADSPA_PORT_OUTPUT|LADSPA_PORT_AUDIO;
        for(int i=4; i<16; i++) pd[i]=LADSPA_PORT_INPUT|LADSPA_PORT_CONTROL;
        g_desc->PortDescriptors = pd;

        const char ** names = (const char **)calloc(16, sizeof(char *));
        names[0]="In L"; names[1]="In R"; names[2]="Out L"; names[3]="Out R";
        names[4]="B1: On"; names[5]="B1: Freq"; names[6]="B1: Q"; names[7]="B1: Depth";
        names[8]="B2: On"; names[9]="B2: Freq"; names[10]="B2: Q"; names[11]="B2: Depth";
        names[12]="B3: On"; names[13]="B3: Freq"; names[14]="B3: Q"; names[15]="B3: Depth";
        g_desc->PortNames = names;

        LADSPA_PortRangeHint * h = (LADSPA_PortRangeHint *)calloc(16, sizeof(LADSPA_PortRangeHint));
        for(int i=0; i<3; i++) {
            int o = 4 + (i*4);
            h[o].HintDescriptor = LADSPA_HINT_TOGGLED;
            h[o+1].LowerBound=100; h[o+1].UpperBound=10000; h[o+1].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MIDDLE;
            h[o+2].LowerBound=0.1; h[o+2].UpperBound=10; h[o+2].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MIDDLE;
            h[o+3].LowerBound=-24; h[o+3].UpperBound=24; h[o+3].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_0;
        }
        g_desc->PortRangeHints = h;
        g_desc->instantiate = instantiateWidth; g_desc->connect_port = connectPortWidth; g_desc->run = runWidth; g_desc->cleanup = cleanupWidth;
    }
    return g_desc;
}
