#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <ladspa.h>

#define GATE_BPM      0
#define GATE_DIV      1  
#define GATE_SMEAR    2
#define GATE_RESET    3  
#define GATE_STEP1    4  // Steps are now 4 through 35
#define GATE_IN_L     36
#define GATE_IN_R     37
#define GATE_OUT_L    38
#define GATE_OUT_R    39

typedef struct {
    LADSPA_Data * ports[40];
    float m_fSR;
    unsigned long sampleCounter;
    int currentStep;
    float env;
    float lastReset; 
} MireGate;

LADSPA_Handle instantiateGate(const LADSPA_Descriptor * Descriptor, unsigned long SampleRate) {
    MireGate * p = (MireGate *)calloc(1, sizeof(MireGate));
    if (p) p->m_fSR = (float)SampleRate;
    return (LADSPA_Handle)p;
}

void connectPortGate(LADSPA_Handle Instance, unsigned long Port, LADSPA_Data * Data) {
    ((MireGate *)Instance)->ports[Port] = Data;
}

void runGate(LADSPA_Handle Instance, unsigned long SampleCount) {
    MireGate * p = (MireGate *)Instance;
    if (!p) return;

    float bpm = *p->ports[GATE_BPM];
    int divMode = (int)(*p->ports[GATE_DIV] + 0.5f);
    float smear = *p->ports[GATE_SMEAR];
    float currentReset = *p->ports[GATE_RESET];

    if (currentReset > 0.5f && p->lastReset <= 0.5f) {
        p->currentStep = 0;
        p->sampleCounter = 0;
    }
    p->lastReset = currentReset;

    float stepsPerBeat = 4.0f;
    if (divMode == 0) stepsPerBeat = 2.0f; // 1/8
    if (divMode == 2) stepsPerBeat = 8.0f; // 1/32

    unsigned long samplesPerStep = (unsigned long)((60.0f / (bpm * stepsPerBeat)) * p->m_fSR);
    if (samplesPerStep < 1) samplesPerStep = 1;

    for (unsigned long i = 0; i < SampleCount; i++) {
        p->sampleCounter++;
        if (p->sampleCounter >= samplesPerStep) {
            p->sampleCounter = 0;
            p->currentStep = (p->currentStep + 1) % 32; // Cycle through 32 steps
        }

        float targetGain = (*p->ports[GATE_STEP1 + p->currentStep] > 0.5f) ? 1.0f : 0.0f;
        float factor = 1.0f - (smear * 0.999f); 
        p->env = (targetGain * factor) + (p->env * (1.0f - factor));

        p->ports[GATE_OUT_L][i] = p->ports[GATE_IN_L][i] * p->env;
        p->ports[GATE_OUT_R][i] = p->ports[GATE_IN_R][i] * p->env;
    }
}

void cleanupGate(LADSPA_Handle Instance) { free(Instance); }

static LADSPA_Descriptor * g_desc = NULL;
const LADSPA_Descriptor * ladspa_descriptor(unsigned long Index) {
    if (Index != 0) return NULL;
    if (!g_desc) {
        g_desc = (LADSPA_Descriptor *)calloc(1, sizeof(LADSPA_Descriptor));
        g_desc->UniqueID = 604008;
        g_desc->Label = strdup("mire_gate");
        g_desc->Name = strdup("Mire Gate");
        g_desc->Maker = strdup("Mire");
        g_desc->PortCount = 40;

        LADSPA_PortDescriptor * pd = (LADSPA_PortDescriptor *)calloc(40, sizeof(LADSPA_PortDescriptor));
        for(int i=0; i<36; i++) pd[i] = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        pd[36]=pd[37] = LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO;
        pd[38]=pd[39] = LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO;
        g_desc->PortDescriptors = pd;

        const char ** names = (const char **)calloc(40, sizeof(char *));
        names[0] = strdup("BPM"); 
        names[1] = strdup("Rate (0:1/8, 1:1/16, 2:1/32)");
        names[2] = strdup("Smear");
        names[3] = strdup("Reset Trigger");
        for(int i=0; i<32; i++) {
            char buf[32]; sprintf(buf, "Step %02d", i+1);
            names[4+i] = strdup(buf);
        }
        names[36] = strdup("In L"); names[37] = strdup("In R"); 
        names[38] = strdup("Out L"); names[39] = strdup("Out R");
        g_desc->PortNames = names;

        LADSPA_PortRangeHint * h = (LADSPA_PortRangeHint *)calloc(40, sizeof(LADSPA_PortRangeHint));
        h[0].LowerBound=100; h[0].UpperBound=320; h[0].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_INTEGER|LADSPA_HINT_DEFAULT_LOW;
        h[1].LowerBound=0; h[1].UpperBound=2; h[1].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_INTEGER|LADSPA_HINT_DEFAULT_1;
        h[2].LowerBound=0; h[2].UpperBound=1; h[2].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_LOW;
        h[3].HintDescriptor = LADSPA_HINT_TOGGLED | LADSPA_HINT_DEFAULT_0; 
        for(int i=4; i<36; i++) h[i].HintDescriptor = LADSPA_HINT_TOGGLED | LADSPA_HINT_DEFAULT_1;
        g_desc->PortRangeHints = h;

        g_desc->instantiate = instantiateGate; g_desc->connect_port = connectPortGate; g_desc->run = runGate; g_desc->cleanup = cleanupGate;
    }
    return g_desc;
}
