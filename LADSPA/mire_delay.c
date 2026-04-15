#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <ladspa.h>

#define ECHO_DRIVE    0
#define ECHO_TIME_MS  1
#define ECHO_FEEDBACK 2
#define ECHO_TONE     3
#define ECHO_BITS     4
#define ECHO_SR_DIV   5
#define ECHO_L_OFFSET 6
#define ECHO_MIX      7
#define ECHO_IN_L     8
#define ECHO_IN_R     9
#define ECHO_OUT_L    10
#define ECHO_OUT_R    11

#define MAX_DELAY 192000

typedef struct {
    LADSPA_Data * ports[12];
    float * bufferL;
    float * bufferR;
    unsigned long writeIdx;
    float lastLP_L;
    float lastLP_R;
    float m_fSR;
    float lastS_L;
    float lastS_R;
    float srCounter;
} MireEcho;

LADSPA_Handle instantiateEcho(const LADSPA_Descriptor * Descriptor, unsigned long SampleRate) {
    MireEcho * p = (MireEcho *)calloc(1, sizeof(MireEcho));
    p->m_fSR = (float)SampleRate;
    p->bufferL = (float *)calloc(MAX_DELAY, sizeof(float));
    p->bufferR = (float *)calloc(MAX_DELAY, sizeof(float));
    return (LADSPA_Handle)p;
}

void connectPortEcho(LADSPA_Handle Instance, unsigned long Port, LADSPA_Data * Data) {
    ((MireEcho *)Instance)->ports[Port] = Data;
}

float saturate(float in, float drive) {
    float x = in * drive;
    if (x > 1.0f) return 1.0f;
    if (x < -1.0f) return -1.0f;
    return x - (x * x * x * 0.333333f); 
}

void runEcho(LADSPA_Handle Instance, unsigned long SampleCount) {
    MireEcho * p = (MireEcho *)Instance;
    
    float drive = 1.0f + (*p->ports[ECHO_DRIVE] * 4.0f);
    float timeMs = *p->ports[ECHO_TIME_MS];
    float feedback = *p->ports[ECHO_FEEDBACK];
    float tone = *p->ports[ECHO_TONE];
    float bits = *p->ports[ECHO_BITS];
    float srDiv = *p->ports[ECHO_SR_DIV];
    float lOffsetMs = *p->ports[ECHO_L_OFFSET];
    float mix = *p->ports[ECHO_MIX];
    float invMix = 1.0f - mix;

    unsigned long baseSamples = (unsigned long)((timeMs / 1000.0f) * p->m_fSR);
    unsigned long lOffsetSamples = (unsigned long)((lOffsetMs / 1000.0f) * p->m_fSR);
    float levels = powf(2.0f, bits);

    for (unsigned long i = 0; i < SampleCount; i++) {
        float inL = p->ports[ECHO_IN_L][i];
        float inR = p->ports[ECHO_IN_R][i];

        unsigned long readR = (p->writeIdx + MAX_DELAY - baseSamples) % MAX_DELAY;
        unsigned long readL = (p->writeIdx + MAX_DELAY - (baseSamples + lOffsetSamples)) % MAX_DELAY;

        float delayedL = p->bufferL[readL];
        float delayedR = p->bufferR[readR];

        p->srCounter += 1.0f;
        if (p->srCounter >= srDiv) {
            p->lastS_L = delayedL;
            p->lastS_R = delayedR;
            p->srCounter = 0;
        }

        float loL = (float)((int)(p->lastS_L * levels)) / levels;
        float loR = (float)((int)(p->lastS_R * levels)) / levels;

        float lpL = (loL * (1.0f - tone)) + (p->lastLP_L * tone);
        float lpR = (loR * (1.0f - tone)) + (p->lastLP_R * tone);
        p->lastLP_L = lpL;
        p->lastLP_R = lpR;

        float feedL = saturate(inL + (lpL * feedback), drive);
        float feedR = saturate(inR + (lpR * feedback), drive);

        p->bufferL[p->writeIdx] = feedL;
        p->bufferR[p->writeIdx] = feedR;
        p->writeIdx = (p->writeIdx + 1) % MAX_DELAY;

        p->ports[ECHO_OUT_L][i] = (inL * invMix) + (lpL * mix);
        p->ports[ECHO_OUT_R][i] = (inR * invMix) + (lpR * mix);
    }
}

void cleanupEcho(LADSPA_Handle Instance) {
    MireEcho * p = (MireEcho *)Instance;
    free(p->bufferL); free(p->bufferR); free(p);
}

static LADSPA_Descriptor * g_desc = NULL;
const LADSPA_Descriptor * ladspa_descriptor(unsigned long Index) {
    if (Index != 0) return NULL;
    if (!g_desc) {
        g_desc = (LADSPA_Descriptor *)calloc(1, sizeof(LADSPA_Descriptor));
        g_desc->UniqueID = 604003;
        g_desc->Label = strdup("mire_delay");
        g_desc->Name = strdup("Mire Delay");
        g_desc->Maker = strdup("Mire");
        g_desc->PortCount = 12;

        LADSPA_PortDescriptor * pd = (LADSPA_PortDescriptor *)calloc(12, sizeof(LADSPA_PortDescriptor));
        for(int i=0; i<8; i++) pd[i] = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        pd[8]=pd[9] = LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO;
        pd[10]=pd[11] = LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO;
        g_desc->PortDescriptors = pd;

        const char ** names = (const char **)calloc(12, sizeof(char *));
        names[0]="Drive"; names[1]="Time (ms)"; names[2]="Feedback"; 
        names[3]="Tone"; names[4]="Bit Depth"; names[5]="SR Reduction"; 
        names[6]="L-Offset (ms)"; names[7]="Mix";
        names[8]="In L"; names[9]="In R"; names[10]="Out L"; names[11]="Out R";
        g_desc->PortNames = names;

        LADSPA_PortRangeHint * h = (LADSPA_PortRangeHint *)calloc(12, sizeof(LADSPA_PortRangeHint));
        h[0].LowerBound=0; h[0].UpperBound=1; h[0].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MINIMUM;
        h[1].LowerBound=1; h[1].UpperBound=900; h[1].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MIDDLE;
        h[2].LowerBound=0; h[2].UpperBound=0.98; h[2].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MIDDLE;
        h[3].LowerBound=0; h[3].UpperBound=1; h[3].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MIDDLE;
        h[4].LowerBound=1; h[4].UpperBound=24; h[4].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MAXIMUM; // Default 24 bit
        h[5].LowerBound=1; h[5].UpperBound=50; h[5].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MINIMUM;  // Default 1 (no reduction)
        h[6].LowerBound=0; h[6].UpperBound=500; h[6].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MINIMUM;
        h[7].LowerBound=0; h[7].UpperBound=1; h[7].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_LOW;
        g_desc->PortRangeHints = h;

        g_desc->instantiate = instantiateEcho; g_desc->connect_port = connectPortEcho; g_desc->run = runEcho; g_desc->cleanup = cleanupEcho;
    }
    return g_desc;
}
