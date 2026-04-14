#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <ladspa.h>

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
    LADSPA_Data * m_pfBPM, * m_pfFeedback, * m_pfFirstL, * m_pfHPF, * m_pfDryWet;
    LADSPA_Data * m_pfInL, * m_pfInR, * m_pfOutL, * m_pfOutR;
    float * m_pfBufferL, * m_pfBufferR;
    unsigned long m_iBufferSize, m_iWritePos;
    float m_fSR;
    Biquad filterL[2], filterR[2]; 
} PingPong;

void update_hpf(Biquad * f, float freq, float sampleRate) {
    float omega = 2.0f * M_PI * freq / sampleRate;
    float sn = sinf(omega);
    float cs = cosf(omega);
    float alpha = sn / (2.0f * 0.7071f); 
    float a0_inv = 1.0f / (1.0f + alpha);
    f->a0 = ((1.0f + cs) * 0.5f) * a0_inv;
    f->a1 = -(1.0f + cs) * a0_inv;
    f->a2 = f->a0;
    f->b1 = (-2.0f * cs) * a0_inv;
    f->b2 = (1.0f - alpha) * a0_inv;
}

float process_biquad(Biquad * f, float in) {
    float out = f->a0 * in + f->a1 * f->x1 + f->a2 * f->x2 - f->b1 * f->y1 - f->b2 * f->y2;
    f->x2 = f->x1; f->x1 = in;
    f->y2 = f->y1; f->y1 = out;
    return out;
}

LADSPA_Handle instantiatePP(const LADSPA_Descriptor * Descriptor, unsigned long SampleRate) {
    PingPong * p = (PingPong *)calloc(1, sizeof(PingPong));
    p->m_fSR = (float)SampleRate;
    p->m_iBufferSize = SampleRate * 2; 
    p->m_pfBufferL = (float *)calloc(p->m_iBufferSize, sizeof(float));
    p->m_pfBufferR = (float *)calloc(p->m_iBufferSize, sizeof(float));
    return (LADSPA_Handle)p;
}

void connectPortPP(LADSPA_Handle Instance, unsigned long Port, LADSPA_Data * Data) {
    PingPong * p = (PingPong *)Instance;
    switch (Port) {
        case PP_BPM:      p->m_pfBPM = Data; break;
        case PP_FEEDBACK: p->m_pfFeedback = Data; break;
        case PP_FIRST_L:  p->m_pfFirstL = Data; break;
        case PP_HPF_FREQ: p->m_pfHPF = Data; break;
        case PP_DRY_WET:  p->m_pfDryWet = Data; break;
        case PP_IN_L:     p->m_pfInL = Data; break;
        case PP_IN_R:     p->m_pfInR = Data; break;
        case PP_OUT_L:    p->m_pfOutL = Data; break;
        case PP_OUT_R:    p->m_pfOutR = Data; break;
    }
}

void runPP(LADSPA_Handle Instance, unsigned long SampleCount) {
    PingPong * p = (PingPong *)Instance;
    float beatLen = 60.0f / (*p->m_pfBPM);
    unsigned long delaySamples = (unsigned long)(beatLen * 0.75f * p->m_fSR);
    float feedback = *p->m_pfFeedback;
    float mix = *p->m_pfDryWet; // 0 to 1
    int startLeft = (*p->m_pfFirstL > 0.5f);
    
    for(int j=0; j<2; j++) {
        update_hpf(&p->filterL[j], *p->m_pfHPF, p->m_fSR);
        update_hpf(&p->filterR[j], *p->m_pfHPF, p->m_fSR);
    }

    for (unsigned long i = 0; i < SampleCount; i++) {
        float inL = p->m_pfInL[i];
        float inR = p->m_pfInR[i];
        float monoIn = (inL + inR) * 0.5f;

        long readPos = (long)p->m_iWritePos - (long)delaySamples;
        if (readPos < 0) readPos += p->m_iBufferSize;

        float echoL = p->m_pfBufferL[readPos];
        float echoR = p->m_pfBufferR[readPos];

        if (startLeft) {
            p->m_pfBufferL[p->m_iWritePos] = monoIn + (echoR * feedback);
            p->m_pfBufferR[p->m_iWritePos] = (feedback <= 0.0f) ? echoL : echoL * feedback;
        } else {
            p->m_pfBufferR[p->m_iWritePos] = monoIn + (echoL * feedback);
            p->m_pfBufferL[p->m_iWritePos] = (feedback <= 0.0f) ? echoR : echoR * feedback;
        }

        float filteredEchoL = process_biquad(&p->filterL[1], process_biquad(&p->filterL[0], echoL));
        float filteredEchoR = process_biquad(&p->filterR[1], process_biquad(&p->filterR[0], echoR));

        // Dry/Wet Mixing: (1-mix) * Dry + (mix) * Wet
        p->m_pfOutL[i] = (inL * (1.0f - mix)) + (filteredEchoL * mix);
        p->m_pfOutR[i] = (inR * (1.0f - mix)) + (filteredEchoR * mix);

        p->m_iWritePos = (p->m_iWritePos + 1) % p->m_iBufferSize;
    }
}

void cleanupPP(LADSPA_Handle Instance) {
    PingPong * p = (PingPong *)Instance;
    free(p->m_pfBufferL); free(p->m_pfBufferR); free(p);
}

static LADSPA_Descriptor * g_desc = NULL;
const LADSPA_Descriptor * ladspa_descriptor(unsigned long Index) {
    if (Index != 0) return NULL;
    if (!g_desc) {
        g_desc = (LADSPA_Descriptor *)calloc(1, sizeof(LADSPA_Descriptor));
        g_desc->UniqueID = 604006;
        g_desc->Label = strdup("mire_delay_pp");
        g_desc->Name = strdup("Mire Delay PP");
        g_desc->Maker = strdup("Mire");
        g_desc->PortCount = 9;
        LADSPA_PortDescriptor * pd = (LADSPA_PortDescriptor *)calloc(9, sizeof(LADSPA_PortDescriptor));
        for(int i=0; i<5; i++) pd[i]=LADSPA_PORT_INPUT|LADSPA_PORT_CONTROL;
        pd[5]=pd[6]=LADSPA_PORT_INPUT|LADSPA_PORT_AUDIO;
        pd[7]=pd[8]=LADSPA_PORT_OUTPUT|LADSPA_PORT_AUDIO;
        g_desc->PortDescriptors = pd;
        const char ** names = (const char **)calloc(9, sizeof(char *));
        names[0]="BPM"; names[1]="Feedback"; names[2]="Start Left Switch"; 
        names[3]="Echo HPF Cutoff"; names[4]="Dry/Wet Mix";
        names[5]="In L"; names[6]="In R"; names[7]="Out L"; names[8]="Out R";
        g_desc->PortNames = names;
        LADSPA_PortRangeHint * h = (LADSPA_PortRangeHint *)calloc(9, sizeof(LADSPA_PortRangeHint));
        h[0].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_LOW|LADSPA_HINT_INTEGER;
        h[0].LowerBound=60; h[0].UpperBound=200;
        h[1].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_LOW;
        h[1].LowerBound=0; h[1].UpperBound=1.0;
        h[2].HintDescriptor=LADSPA_HINT_TOGGLED|LADSPA_HINT_DEFAULT_1;
        h[3].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_LOW|LADSPA_HINT_LOGARITHMIC;
        h[3].LowerBound=20; h[3].UpperBound=10000;
        h[4].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MIDDLE;
        h[4].LowerBound=0; h[4].UpperBound=1.0;
        g_desc->PortRangeHints = h;
        g_desc->instantiate = instantiatePP; g_desc->connect_port = connectPortPP; g_desc->run = runPP; g_desc->cleanup = cleanupPP;
    }
    return g_desc;
}
