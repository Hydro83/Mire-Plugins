#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <ladspa.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define PHASE_RATE    0
#define PHASE_DEPTH   1
#define PHASE_MANUAL  2
#define PHASE_FEEDBK  3
#define PHASE_DELAY_ON 4  // NEW: Toggle Button
#define PHASE_DELAY   5
#define PHASE_STAGES  6
#define PHASE_IN_L    7
#define PHASE_IN_R    8
#define PHASE_OUT_L   9
#define PHASE_OUT_R   10

#define MAX_DELAY 48000 

typedef struct {
    float x1, y1;
} APFilter;

typedef struct {
    LADSPA_Data * ports[11];
    float m_fSR;
    float lfo_phase;
    APFilter filtersL[12];
    APFilter filtersR[12];
    
    float * delayBufL;
    float * delayBufR;
    unsigned long writeIdx;
    
    // Direct feedback storage for "Off" mode
    float lastOutL, lastOutR;
} MirePhase;

static inline float process_ap(APFilter *f, float in, float g) {
    float out = -g * in + f->x1 + g * f->y1;
    f->x1 = in;
    f->y1 = out;
    return out;
}

LADSPA_Handle instantiatePhase(const LADSPA_Descriptor * Descriptor, unsigned long SampleRate) {
    MirePhase * p = (MirePhase *)calloc(1, sizeof(MirePhase));
    if (p) {
        p->m_fSR = (float)SampleRate;
        p->delayBufL = (float *)calloc(MAX_DELAY, sizeof(float));
        p->delayBufR = (float *)calloc(MAX_DELAY, sizeof(float));
    }
    return (LADSPA_Handle)p;
}

void connectPortPhase(LADSPA_Handle Instance, unsigned long Port, LADSPA_Data * Data) {
    ((MirePhase *)Instance)->ports[Port] = Data;
}

void runPhase(LADSPA_Handle Instance, unsigned long SampleCount) {
    MirePhase * p = (MirePhase *)Instance;
    if (!p) return;

    float rate = *p->ports[PHASE_RATE];
    float depth = *p->ports[PHASE_DEPTH]; 
    float manual = *p->ports[PHASE_MANUAL];
    float fb = *p->ports[PHASE_FEEDBK];
    int delayOn = (*p->ports[PHASE_DELAY_ON] > 0.5f);
    float delayMs = *p->ports[PHASE_DELAY];
    int stages = (int)(*p->ports[PHASE_STAGES] + 0.5f);
    if (stages < 1) stages = 1;
    if (stages > 12) stages = 12;

    long delaySamps = (long)(delayMs * 0.001f * p->m_fSR);
    if (delaySamps >= MAX_DELAY) delaySamps = MAX_DELAY - 1;

    for (unsigned long i = 0; i < SampleCount; i++) {
        p->lfo_phase += (6.283185f * rate) / p->m_fSR;
        if (p->lfo_phase > 6.283185f) p->lfo_phase -= 6.283185f;
        
        float lfo_mod = sinf(p->lfo_phase) * depth;
        float combined = fmaxf(0.0f, fminf(1.0f, manual + lfo_mod));

        float freq = 50.0f * powf(15000.0f / 50.0f, combined);
        float tan_val = tanf(M_PI * freq / p->m_fSR);
        float g = (tan_val - 1.0f) / (tan_val + 1.0f);

        float fbSigL, fbSigR;

        if (delayOn) {
            // Use Buffer for DP/4 Sound
            long readIdx = (long)p->writeIdx - delaySamps;
            if (readIdx < 0) readIdx += MAX_DELAY;
            fbSigL = p->delayBufL[readIdx];
            fbSigR = p->delayBufR[readIdx];
        } else {
            // Use Direct Feedback for Standard Sound
            fbSigL = p->lastOutL;
            fbSigR = p->lastOutR;
        }

        float inL = p->ports[PHASE_IN_L][i] + (fbSigL * fb);
        float inR = p->ports[PHASE_IN_R][i] + (fbSigR * fb);

        float stageL = inL; float stageR = inR;
        for (int s = 0; s < stages; s++) {
            stageL = process_ap(&p->filtersL[s], stageL, g);
            stageR = process_ap(&p->filtersR[s], stageR, g);
        }

        // Always update both for seamless switching
        p->lastOutL = stageL;
        p->lastOutR = stageR;
        p->delayBufL[p->writeIdx] = stageL;
        p->delayBufR[p->writeIdx] = stageR;
        p->writeIdx = (p->writeIdx + 1) % MAX_DELAY;

        p->ports[PHASE_OUT_L][i] = (p->ports[PHASE_IN_L][i] + stageL) * 0.5f;
        p->ports[PHASE_OUT_R][i] = (p->ports[PHASE_IN_R][i] + stageR) * 0.5f;
    }
}

void cleanupPhase(LADSPA_Handle Instance) {
    MirePhase * p = (MirePhase *)Instance;
    free(p->delayBufL);
    free(p->delayBufR);
    free(p);
}

static LADSPA_Descriptor * g_desc = NULL;
const LADSPA_Descriptor * ladspa_descriptor(unsigned long Index) {
    if (Index != 0) return NULL;
    if (!g_desc) {
        g_desc = (LADSPA_Descriptor *)calloc(1, sizeof(LADSPA_Descriptor));
        g_desc->UniqueID = 604002;
        g_desc->Label = strdup("mire_phaser");
        g_desc->Name = strdup("Mire Phaser");
        g_desc->Maker = strdup("Mire");
        g_desc->PortCount = 11;

        LADSPA_PortDescriptor * pd = (LADSPA_PortDescriptor *)calloc(11, sizeof(LADSPA_PortDescriptor));
        for(int i=0; i<7; i++) pd[i] = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        pd[7]=pd[8] = LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO;
        pd[9]=pd[10] = LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO;
        g_desc->PortDescriptors = pd;

        const char ** names = (const char **)calloc(11, sizeof(char *));
        names[0]="Rate"; names[1]="Depth"; names[2]="Manual"; names[3]="Feedback"; 
        names[4]="FB Delay On"; names[5]="FB Delay (ms)"; names[6]="Stages";
        names[7]="In L"; names[8]="In R"; names[9]="Out L"; names[10]="Out R";
        g_desc->PortNames = names;

        LADSPA_PortRangeHint * h = (LADSPA_PortRangeHint *)calloc(11, sizeof(LADSPA_PortRangeHint));
        h[0].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MINIMUM;
        h[0].LowerBound=0.01; h[0].UpperBound=0.6;
        h[1].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MIDDLE;
        h[1].LowerBound=0.0; h[1].UpperBound=1.0;
        h[2].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MIDDLE;
        h[2].LowerBound=0.0; h[2].UpperBound=1.0;
        h[3].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MIDDLE;
        h[3].LowerBound=-0.98; h[3].UpperBound=0.98;
        
        // Toggle: 0 or 1
        h[4].HintDescriptor = LADSPA_HINT_TOGGLED | LADSPA_HINT_DEFAULT_0;
        
        h[5].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_LOW;
        h[5].LowerBound=0.0; h[5].UpperBound=10.0;
        h[6].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_INTEGER|LADSPA_HINT_DEFAULT_MAXIMUM;
        h[6].LowerBound=1; h[6].UpperBound=12;
        g_desc->PortRangeHints = h;

        g_desc->instantiate = instantiatePhase; g_desc->connect_port = connectPortPhase; g_desc->run = runPhase; g_desc->cleanup = cleanupPhase;
    }
    return g_desc;
}
