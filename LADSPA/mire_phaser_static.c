#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <ladspa.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define PHASER_FREQ      0
#define PHASER_FEEDBACK  1
#define PHASER_STAGES    2
#define PHASER_WET       3
#define PHASER_DRY       4
#define PHASER_IN_L      5
#define PHASER_IN_R      6
#define PHASER_OUT_L     7
#define PHASER_OUT_R     8

typedef struct {
    float x1;
    float y1;
} AllPass;

typedef struct {
    LADSPA_Data * ports[9];
    float sample_rate;
    AllPass filtersL[12];
    AllPass filtersR[12];
    float last_feedbackL;
    float last_feedbackR;
} MirePhaser;

LADSPA_Handle instantiatePhaser(const LADSPA_Descriptor * Descriptor, unsigned long SampleRate) {
    MirePhaser * p = (MirePhaser *)calloc(1, sizeof(MirePhaser));
    if (!p) return NULL;
    p->sample_rate = (float)SampleRate;
    return (LADSPA_Handle)p;
}

void connectPortPhaser(LADSPA_Handle Instance, unsigned long Port, LADSPA_Data * Data) {
    ((MirePhaser *)Instance)->ports[Port] = Data;
}

void runPhaser(LADSPA_Handle Instance, unsigned long SampleCount) {
    MirePhaser * p = (MirePhaser *)Instance;

    float freq = *p->ports[PHASER_FREQ];
    float feedback = *p->ports[PHASER_FEEDBACK];
    int stages = (int)(*p->ports[PHASER_STAGES] + 0.5f);
    float wetAmt = *p->ports[PHASER_WET];
    float dryAmt = *p->ports[PHASER_DRY];

    if (stages < 1) stages = 1;
    if (stages > 12) stages = 12;

    float omega = tanf(M_PI * freq / p->sample_rate);
    float alpha = (omega - 1.0f) / (omega + 1.0f);

    for (unsigned long i = 0; i < SampleCount; i++) {
        float inL = p->ports[PHASER_IN_L][i];
        float inR = p->ports[PHASER_IN_R][i];

        float stage_inL = inL + p->last_feedbackL * feedback;
        float stage_inR = inR + p->last_feedbackR * feedback;

        for (int s = 0; s < stages; s++) {
            float outL = alpha * stage_inL + p->filtersL[s].x1 - alpha * p->filtersL[s].y1;
            if (fabsf(outL) < 1e-15f) outL = 0.0f;
            p->filtersL[s].x1 = stage_inL;
            p->filtersL[s].y1 = outL;
            stage_inL = outL;

            float outR = alpha * stage_inR + p->filtersR[s].x1 - alpha * p->filtersR[s].y1;
            if (fabsf(outR) < 1e-15f) outR = 0.0f;
            p->filtersR[s].x1 = stage_inR;
            p->filtersR[s].y1 = outR;
            stage_inR = outR;
        }

        p->last_feedbackL = stage_inL;
        p->last_feedbackR = stage_inR;

        p->ports[PHASER_OUT_L][i] = (inL * dryAmt) + (stage_inL * wetAmt);
        p->ports[PHASER_OUT_R][i] = (inR * dryAmt) + (stage_inR * wetAmt);
    }
}

void cleanupPhaser(LADSPA_Handle Instance) {
    free(Instance);
}

static LADSPA_Descriptor * g_desc = NULL;
const LADSPA_Descriptor * ladspa_descriptor(unsigned long Index) {
    if (Index != 0) return NULL;
    if (!g_desc) {
        g_desc = (LADSPA_Descriptor *)calloc(1, sizeof(LADSPA_Descriptor));
        g_desc->UniqueID = 604011;
        g_desc->Label = strdup("mire_phaser_static");
        g_desc->Name = strdup("Mire Static Phaser");
        g_desc->Maker = strdup("Mire");
        g_desc->PortCount = 9;

        LADSPA_PortDescriptor * pd = (LADSPA_PortDescriptor *)calloc(9, sizeof(LADSPA_PortDescriptor));
        for(int i=0; i<5; i++) pd[i] = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        pd[5]=pd[6] = LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO;
        pd[7]=pd[8] = LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO;
        g_desc->PortDescriptors = pd;

        const char ** names = (const char **)calloc(9, sizeof(char *));
        names[0]="Center Freq"; names[1]="Feedback"; names[2]="Stages"; 
        names[3]="Wet Amount"; names[4]="Dry Amount";
        names[5]="In L"; names[6]="In R"; names[7]="Out L"; names[8]="Out R";
        g_desc->PortNames = names;

        LADSPA_PortRangeHint * h = (LADSPA_PortRangeHint *)calloc(9, sizeof(LADSPA_PortRangeHint));
        
        h[0].LowerBound = 20; h[0].UpperBound = 10000; 
        h[0].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_LOGARITHMIC | LADSPA_HINT_DEFAULT_MIDDLE;
        h[1].LowerBound = -0.95; h[1].UpperBound = 0.95; 
        h[1].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_DEFAULT_0;
        h[2].LowerBound = 1; h[2].UpperBound = 12; 
        h[2].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_INTEGER | LADSPA_HINT_DEFAULT_MAXIMUM;
        h[3].LowerBound = 0; h[3].UpperBound = 2; h[3].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_DEFAULT_MIDDLE;
        h[4].LowerBound = 0; h[4].UpperBound = 2; h[4].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_DEFAULT_MIDDLE;
        
        g_desc->PortRangeHints = h;
        g_desc->instantiate = instantiatePhaser; 
        g_desc->connect_port = connectPortPhaser; 
        g_desc->run = runPhaser; 
        g_desc->cleanup = cleanupPhaser;
    }
    return g_desc;
}
