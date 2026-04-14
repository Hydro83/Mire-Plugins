#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <ladspa.h>

#define VERB_SIZE      0
#define VERB_DECAY     1
#define VERB_DAMP      2
#define VERB_MOD_ON    3
#define VERB_MOD_RATE  4
#define VERB_MOD_DEPTH 5
#define VERB_MIX       6
#define VERB_IN_L      7
#define VERB_IN_R      8
#define VERB_OUT_L     9
#define VERB_OUT_R     10

#define NUM_DELAYS 8
#define MAX_BUF 32768
#define MOD_BUF 4096 

// Prime numbers for high density and low resonance
const int DELAY_SAMPLES[NUM_DELAYS] = {1531, 1907, 2153, 2459, 2687, 2903, 3163, 3391};

typedef struct {
    LADSPA_Data * ports[11];
    float m_fSR;
    float * buffers[NUM_DELAYS];
    unsigned long writeIdx[NUM_DELAYS];
    float lastLP[NUM_DELAYS];
    
    // Vibrato components
    float * modBuffer;
    unsigned long modWriteIdx;
    float lfoPhase;
} MireVerb;

LADSPA_Handle instantiateVerb(const LADSPA_Descriptor * Descriptor, unsigned long SampleRate) {
    MireVerb * p = (MireVerb *)calloc(1, sizeof(MireVerb));
    if (!p) return NULL;
    p->m_fSR = (float)SampleRate;
    for(int i=0; i<NUM_DELAYS; i++) {
        p->buffers[i] = (float *)calloc(MAX_BUF, sizeof(float));
    }
    p->modBuffer = (float *)calloc(MOD_BUF, sizeof(float));
    return (LADSPA_Handle)p;
}

void connectPortVerb(LADSPA_Handle Instance, unsigned long Port, LADSPA_Data * Data) {
    ((MireVerb *)Instance)->ports[Port] = Data;
}

void runVerb(LADSPA_Handle Instance, unsigned long SampleCount) {
    MireVerb * p = (MireVerb *)Instance;
    if (!p) return;

    float size = *p->ports[VERB_SIZE];    
    float decay = *p->ports[VERB_DECAY];  
    float damp = *p->ports[VERB_DAMP];
    int modOn = (*p->ports[VERB_MOD_ON] > 0.5f);
    float modRate = *p->ports[VERB_MOD_RATE];
    float modDepth = *p->ports[VERB_MOD_DEPTH];
    float mix = *p->ports[VERB_MIX];

    for (unsigned long i = 0; i < SampleCount; i++) {
        float inL = p->ports[VERB_IN_L][i];
        float inR = p->ports[VERB_IN_R][i];
        float monoIn = (inL + inR) * 0.5f;

        // --- Vibrato Stage ---
        float verbInput = monoIn;
        if (modOn) {
            p->modBuffer[p->modWriteIdx] = monoIn;
            p->lfoPhase += (6.283185f * modRate) / p->m_fSR;
            if (p->lfoPhase > 6.283185f) p->lfoPhase -= 6.283185f;

            float delayMod = (sinf(p->lfoPhase) + 1.0f) * modDepth * 0.0025f * p->m_fSR;
            float readPos = (float)p->modWriteIdx - delayMod;
            while (readPos < 0) readPos += MOD_BUF;
            
            int iPos = (int)readPos;
            float frac = readPos - iPos;
            verbInput = p->modBuffer[iPos % MOD_BUF] * (1.0f - frac) + p->modBuffer[(iPos + 1) % MOD_BUF] * frac;
            p->modWriteIdx = (p->modWriteIdx + 1) % MOD_BUF;
        }

        // --- Denser Reverb Engine ---
        float outputs[NUM_DELAYS];
        float sum = 0;
        
        for(int j=0; j<NUM_DELAYS; j++) {
            int len = (int)(DELAY_SAMPLES[j] * size);
            if (len >= MAX_BUF) len = MAX_BUF - 1;
            unsigned long readIdx = (p->writeIdx[j] + MAX_BUF - len) % MAX_BUF;
            outputs[j] = p->buffers[j][readIdx];
            sum += outputs[j];
        }

        float res = sum * (1.0f / (float)NUM_DELAYS); // Average for feedback stability
        
        for(int j=0; j<NUM_DELAYS; j++) {
            float val = (outputs[j] - res) * decay;
            // Damping / Low Pass
            p->lastLP[j] = (verbInput + val) * (1.0f - damp) + p->lastLP[j] * damp;
            p->buffers[j][p->writeIdx[j]] = p->lastLP[j];
            p->writeIdx[j] = (p->writeIdx[j] + 1) % MAX_BUF;
        }

        // Stereo spreading the 8 taps
        float wetL = (outputs[0] + outputs[2] + outputs[4] + outputs[6]) * 0.4f;
        float wetR = (outputs[1] + outputs[3] + outputs[5] + outputs[7]) * 0.4f;

        p->ports[VERB_OUT_L][i] = inL * (1.0f - mix) + wetL * mix;
        p->ports[VERB_OUT_R][i] = inR * (1.0f - mix) + wetR * mix;
    }
}

void cleanupVerb(LADSPA_Handle Instance) {
    MireVerb * p = (MireVerb *)Instance;
    for(int i=0; i<NUM_DELAYS; i++) free(p->buffers[i]);
    free(p->modBuffer);
    free(p);
}

static LADSPA_Descriptor * g_desc = NULL;
const LADSPA_Descriptor * ladspa_descriptor(unsigned long Index) {
    if (Index != 0) return NULL;
    if (!g_desc) {
        g_desc = (LADSPA_Descriptor *)calloc(1, sizeof(LADSPA_Descriptor));
        g_desc->UniqueID = 604015;
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
        names[0]="Room Size"; names[1]="Decay"; names[2]="Damping"; names[3]="Mod On"; 
        names[4]="Mod Rate (Hz)"; names[5]="Mod Depth"; names[6]="Mix";
        names[7]="In L"; names[8]="In R"; names[9]="Out L"; names[10]="Out R";
        g_desc->PortNames = names;

        LADSPA_PortRangeHint * h = (LADSPA_PortRangeHint *)calloc(11, sizeof(LADSPA_PortRangeHint));
        h[0].LowerBound=0.1; h[0].UpperBound=4.0; h[0].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MIDDLE;
        h[1].LowerBound=0.0; h[1].UpperBound=0.99; h[1].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MIDDLE;
        h[2].LowerBound=0.0; h[2].UpperBound=1.0; h[2].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_LOW;
        h[3].HintDescriptor = LADSPA_HINT_TOGGLED | LADSPA_HINT_DEFAULT_0;
        h[4].LowerBound=0.1; h[4].UpperBound=10.0; h[4].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_LOW;
        h[5].LowerBound=0.0; h[5].UpperBound=1.0; h[5].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_LOW;
        h[6].LowerBound=0.0; h[6].UpperBound=1.0; h[6].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_LOW;
        g_desc->PortRangeHints = h;

        g_desc->instantiate = instantiateVerb; 
        g_desc->connect_port = connectPortVerb; 
        g_desc->run = runVerb; 
        g_desc->cleanup = cleanupVerb;
    }
    return g_desc;
}
