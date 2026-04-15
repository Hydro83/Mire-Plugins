#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <ladspa.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define PP_BPM      0
#define PP_FEEDBACK 1
#define PP_FIRST_L  2
#define PP_HPF_FREQ 3
#define PP_DRY_WET  4
#define PP_IN_L     5
#define PP_IN_R     6
#define PP_OUT_L    7
#define PP_OUT_R    8

typedef struct {
    float a0, a1, a2, b1, b2;
    float x1, x2, y1, y2;
} Biquad;

typedef struct {
    LADSPA_Data * ports[9];
    float * m_pfBufferL, * m_pfBufferR;
    unsigned long m_iBufferSize, m_iWritePos;
    float m_fSR;
    Biquad filterL[2], filterR[2]; 
} PingPong;

void update_hpf(Biquad * f, float freq, float sampleRate) {
    float omega = 2.0f * M_PI * freq / sampleRate;
    float sn = sinf(omega), cs = cosf(omega);
    float alpha = sn / (2.0f * 0.7071f); 
    float a0_inv = 1.0f / (1.0f + alpha);
    f->a0 = ((1.0f + cs) * 0.5f) * a0_inv;
    f->a1 = -(1.0f + cs) * a0_inv;
    f->a2 = f->a0;
    f->b1 = (-2.0f * cs) * a0_inv;
    f->b2 = (1.0f - alpha) * a0_inv;
}

inline float process_biquad(Biquad * f, float in) {
    float out = f->a0 * in + f->a1 * f->x1 + f->a2 * f->x2 - f->b1 * f->y1 - f->b2 * f->y2;
    if (fabsf(out) < 1e-15f) out = 0.0f;
    f->x2 = f->x1; f->x1 = in;
    f->y2 = f->y1; f->y1 = out;
    return out;
}

LADSPA_Handle instantiatePP(const LADSPA_Descriptor * Descriptor, unsigned long SampleRate) {
    PingPong * p = (PingPong *)calloc(1, sizeof(PingPong));
    if (!p) return NULL;
    p->m_fSR = (float)SampleRate;
    p->m_iBufferSize = SampleRate * 2; 
    p->m_pfBufferL = (float *)calloc(p->m_iBufferSize, sizeof(float));
    p->m_pfBufferR = (float *)calloc(p->m_iBufferSize, sizeof(float));
    return (LADSPA_Handle)p;
}

void connectPortPP(LADSPA_Handle Instance, unsigned long Port, LADSPA_Data * Data) {
    ((PingPong *)Instance)->ports[Port] = Data;
}

void runPP(LADSPA_Handle Instance, unsigned long SampleCount) {
    PingPong * p = (PingPong *)Instance;

    float beatLen = 60.0f / (*p->ports[PP_BPM]);
    unsigned long delaySamples = (unsigned long)(beatLen * 0.75f * p->m_fSR);
    float feedback = *p->ports[PP_FEEDBACK];
    float mix = *p->ports[PP_DRY_WET];
    float invMix = 1.0f - mix;
    int startLeft = (*p->ports[PP_FIRST_L] > 0.5f);
    float hpfFreq = *p->ports[PP_HPF_FREQ];

    for(int j=0; j<2; j++) {
        update_hpf(&p->filterL[j], hpfFreq, p->m_fSR);
        update_hpf(&p->filterR[j], hpfFreq, p->m_fSR);
    }

    for (unsigned long i = 0; i < SampleCount; i++) {
        float inL = p->ports[PP_IN_L][i];
        float inR = p->ports[PP_IN_R][i];
        float monoIn = (inL + inR) * 0.5f;

        long readPos = (long)p->m_iWritePos - (long)delaySamples;
        if (readPos < 0) readPos += p->m_iBufferSize;

        float echoL = p->m_pfBufferL[readPos];
        float echoR = p->m_pfBufferR[readPos];

        if (startLeft) {
            p->m_pfBufferL[p->m_iWritePos] = monoIn + (echoR * feedback);
            p->m_pfBufferR[p->m_iWritePos] = echoL; 
        } else {
            p->m_pfBufferR[p->m_iWritePos] = monoIn + (echoL * feedback);
            p->m_pfBufferL[p->m_iWritePos] = echoR;
        }

        float filteredEchoL = process_biquad(&p->filterL[1], process_biquad(&p->filterL[0], echoL));
        float filteredEchoR = process_biquad(&p->filterR[1], process_biquad(&p->filterR[0], echoR));

        p->ports[PP_OUT_L][i] = (inL * invMix) + (filteredEchoL * mix);
        p->ports[PP_OUT_R][i] = (inR * invMix) + (filteredEchoR * mix);

        p->m_iWritePos = (p->m_iWritePos + 1) % p->m_iBufferSize;
    }
}

void cleanupPP(LADSPA_Handle Instance) {
    PingPong * p = (PingPong *)Instance;
    free(p->m_pfBufferL);
    free(p->m_pfBufferR);
    free(p);
}

static LADSPA_Descriptor * g_desc = NULL;
const LADSPA_Descriptor * ladspa_descriptor(unsigned long Index) {
    if (Index != 0) return NULL;
    if (!g_desc) {
        g_desc = (LADSPA_Descriptor *)calloc(1, sizeof(LADSPA_Descriptor));
        g_desc->UniqueID = 604004;
        g_desc->Label = strdup("mire_delay_pp");
        g_desc->Name = strdup("Mire Delay PP");
        g_desc->Maker = strdup("Mire");
        g_desc->PortCount = 9;

        LADSPA_PortDescriptor * pd = (LADSPA_PortDescriptor *)calloc(9, sizeof(LADSPA_PortDescriptor));
        for(int i=0; i<5; i++) pd[i] = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        pd[5]=pd[6] = LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO;
        pd[7]=pd[8] = LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO;
        g_desc->PortDescriptors = pd;

        const char ** names = (const char **)calloc(9, sizeof(char *));
        names[0]="BPM"; names[1]="Feedback"; names[2]="Start Left Switch"; 
        names[3]="Echo HPF Cutoff"; names[4]="Dry/Wet Mix";
        names[5]="In L"; names[6]="In R"; names[7]="Out L"; names[8]="Out R";
        g_desc->PortNames = names;

        LADSPA_PortRangeHint * h = (LADSPA_PortRangeHint *)calloc(9, sizeof(LADSPA_PortRangeHint));
        h[0].LowerBound=60; h[0].UpperBound=200; h[0].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_LOW|LADSPA_HINT_INTEGER;
        h[1].LowerBound=0; h[1].UpperBound=1.0; h[1].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_LOW;
        h[2].HintDescriptor=LADSPA_HINT_TOGGLED|LADSPA_HINT_DEFAULT_1;
        h[3].LowerBound=20; h[3].UpperBound=10000; h[3].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_LOW|LADSPA_HINT_LOGARITHMIC;
        h[4].LowerBound=0; h[4].UpperBound=1.0; h[4].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MIDDLE;
        g_desc->PortRangeHints = h;

        g_desc->instantiate = instantiatePP; 
        g_desc->connect_port = connectPortPP; 
        g_desc->run = runPP; 
        g_desc->cleanup = cleanupPP;
    }
    return g_desc;
}
