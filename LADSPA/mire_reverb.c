#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <ladspa.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define VERB_SIZE      0
#define VERB_DECAY     1
#define VERB_DAMP      2
#define VERB_PREDELAY  3
#define VERB_MOD_RATE  4
#define VERB_MOD_DEPTH 5
#define VERB_MIX       6
#define VERB_IN_L      7
#define VERB_IN_R      8
#define VERB_OUT_L     9
#define VERB_OUT_R     10

#define NUM_DELAYS 8
#define MAX_BUF 65536 
#define PRE_BUF 16384

const int DELAY_SAMPLES[NUM_DELAYS] = {1531, 1907, 2153, 2459, 2687, 2903, 3163, 3391};

typedef struct {
    LADSPA_Data * ports[11];
    float m_fSR;
    float * buffers[NUM_DELAYS];
    unsigned long writeIdx[NUM_DELAYS];
    
    float * preBuffer;
    unsigned long preWriteIdx;
    
    float lpState[NUM_DELAYS];
    float lfoPhase;
} MireVerb;

LADSPA_Handle instantiateVerb(const LADSPA_Descriptor * Descriptor, unsigned long SampleRate) {
    MireVerb * p = (MireVerb *)calloc(1, sizeof(MireVerb));
    if (!p) return NULL;
    p->m_fSR = (float)SampleRate;
    for(int i=0; i<NUM_DELAYS; i++) p->buffers[i] = (float *)calloc(MAX_BUF, sizeof(float));
    p->preBuffer = (float *)calloc(PRE_BUF, sizeof(float));
    return (LADSPA_Handle)p;
}

void connectPortVerb(LADSPA_Handle Instance, unsigned long Port, LADSPA_Data * Data) {
    ((MireVerb *)Instance)->ports[Port] = Data;
}

void runVerb(LADSPA_Handle Instance, unsigned long SampleCount) {
    MireVerb * p = (MireVerb *)Instance;
    
    float size = *p->ports[VERB_SIZE];    
    float decay = *p->ports[VERB_DECAY];  
    float dampInput = *p->ports[VERB_DAMP];
    float preDelayMs = *p->ports[VERB_PREDELAY];
    float modRate = *p->ports[VERB_MOD_RATE];
    float modDepth = *p->ports[VERB_MOD_DEPTH];
    float mix = *p->ports[VERB_MIX];

    float dampCoeff = powf(dampInput, 0.5f) * 0.95f;
    unsigned long preSamples = (unsigned long)(preDelayMs * 0.001f * p->m_fSR);
    float lfoInc = (2.0f * M_PI * modRate) / p->m_fSR;

    for (unsigned long i = 0; i < SampleCount; i++) {
        float monoIn = (p->ports[VERB_IN_L][i] + p->ports[VERB_IN_R][i]) * 0.5f + 1e-18f; 

        p->preBuffer[p->preWriteIdx] = monoIn;
        unsigned long preReadIdx = (p->preWriteIdx + PRE_BUF - preSamples) % PRE_BUF;
        float verbInput = p->preBuffer[preReadIdx];
        p->preWriteIdx = (p->preWriteIdx + 1) % PRE_BUF;

        p->lfoPhase += lfoInc;
        if (p->lfoPhase > 2.0f * M_PI) p->lfoPhase -= 2.0f * M_PI;

        float out[NUM_DELAYS];
        float householder_sum = 0;

        for(int j=0; j<NUM_DELAYS; j++) {
            float mod = (sinf(p->lfoPhase + j) * modDepth * 12.0f);
            float len = (DELAY_SAMPLES[j] * size) + mod;
            float readPos = (float)p->writeIdx[j] - len;
            while(readPos < 0) readPos += (float)MAX_BUF;
            
            int i0 = (int)readPos % MAX_BUF;
            int i1 = (i0 + 1) % MAX_BUF;
            float frac = readPos - (int)readPos;
            out[j] = p->buffers[j][i0] * (1.0f - frac) + p->buffers[j][i1] * frac;
            householder_sum += out[j];
        }

        householder_sum *= (2.0f / (float)NUM_DELAYS);
        
        for(int j=0; j<NUM_DELAYS; j++) {
            float total_sig = verbInput + (out[j] - householder_sum) * decay;
            
            p->lpState[j] = (total_sig * (1.0f - dampCoeff)) + (p->lpState[j] * dampCoeff);
            
            p->buffers[j][p->writeIdx[j]] = p->lpState[j];
            p->writeIdx[j] = (p->writeIdx[j] + 1) % MAX_BUF;
        }

        float wetL = (out[0] + out[2] + out[4] + out[6]) * 0.35f;
        float wetR = (out[1] + out[3] + out[5] + out[7]) * 0.35f;

        p->ports[VERB_OUT_L][i] = p->ports[VERB_IN_L][i] * (1.0f - mix) + wetL * mix;
        p->ports[VERB_OUT_R][i] = p->ports[VERB_IN_R][i] * (1.0f - mix) + wetR * mix;
    }
}

void cleanupVerb(LADSPA_Handle Instance) {
    MireVerb * p = (MireVerb *)Instance;
    for(int i=0; i<NUM_DELAYS; i++) free(p->buffers[i]);
    free(p->preBuffer);
    free(p);
}

static LADSPA_Descriptor * g_desc = NULL;
const LADSPA_Descriptor * ladspa_descriptor(unsigned long Index) {
    if (Index != 0) return NULL;
    if (!g_desc) {
        g_desc = (LADSPA_Descriptor *)calloc(1, sizeof(LADSPA_Descriptor));
        g_desc->UniqueID = 604013;
        g_desc->Label = strdup("mire_reverb");
        g_desc->Name = strdup("Mire Reverb");
        g_desc->Maker = strdup("Mire");
        g_desc->PortCount = 11;

        LADSPA_PortDescriptor * pd = (LADSPA_PortDescriptor *)calloc(11, sizeof(LADSPA_PortDescriptor));
        for(int i=0; i<7; i++) pd[i] = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        pd[7]=pd[8] = LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO;
        pd[9]=pd[10] = LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO;
        g_desc->PortDescriptors = pd;

        const char ** names = (const char **)calloc(11, sizeof(char *));
        names[0]="Room Size"; names[1]="Decay"; names[2]="Damping"; 
        names[3]="Pre-delay (ms)"; names[4]="Mod Rate"; names[5]="Mod Depth"; names[6]="Mix";
        names[7]="In L"; names[8]="In R"; names[9]="Out L"; names[10]="Out R";
        g_desc->PortNames = names;

        LADSPA_PortRangeHint * h = (LADSPA_PortRangeHint *)calloc(11, sizeof(LADSPA_PortRangeHint));
        h[0].LowerBound=0.5; h[0].UpperBound=5.0; h[0].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MIDDLE;
        h[1].LowerBound=0.1; h[1].UpperBound=0.98; h[1].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_HIGH;
        h[2].LowerBound=0.0; h[2].UpperBound=1.0; h[2].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_LOW;
        h[3].LowerBound=0.0; h[3].UpperBound=100.0; h[3].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_LOW;
        h[4].LowerBound=0.1; h[4].UpperBound=2.0; h[4].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MIDDLE;
        h[5].LowerBound=0.0; h[5].UpperBound=5.0; h[5].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MAXIMUM;
        h[6].LowerBound=0.0; h[6].UpperBound=1.0; h[6].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_LOW;
        g_desc->PortRangeHints = h;

        g_desc->instantiate = instantiateVerb; 
        g_desc->connect_port = connectPortVerb; 
        g_desc->run = runVerb; 
        g_desc->cleanup = cleanupVerb;
    }
    return g_desc;
}
