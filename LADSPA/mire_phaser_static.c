#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <ladspa.h>

#define PHASER_FREQ      0
#define PHASER_FEEDBACK  1
#define PHASER_STAGES    2
#define PHASER_AMOUNT    3
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
    // Max 12 stages
    AllPass filtersL[12];
    AllPass filtersR[12];
    float last_feedbackL;
    float last_feedbackR;
} MirePhaser;

LADSPA_Handle instantiatePhaser(const LADSPA_Descriptor * Descriptor, unsigned long SampleRate) {
    MirePhaser * p = (MirePhaser *)calloc(1, sizeof(MirePhaser));
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
    float amount = *p->ports[PHASER_AMOUNT];
    float dry = *p->ports[PHASER_DRY];

    // Calculate all-pass coefficient (Alpha) based on center frequency
    // This matches the standard trapezoidal integration/bilinear transform approach
    float omega = tanf(M_PI * freq / p->sample_rate);
    float alpha = (omega - 1.0f) / (omega + 1.0f);

    for (unsigned long i = 0; i < SampleCount; i++) {
        float inL = p->ports[PHASER_IN_L][i];
        float inR = p->ports[PHASER_IN_R][i];

        // Apply feedback from the previous sample
        float stage_inL = inL + p->last_feedbackL * feedback;
        float stage_inR = inR + p->last_feedbackR * feedback;

        // Chain the All-pass stages
        for (int s = 0; s < stages; s++) {
            // Left Channel
            float outL = alpha * stage_inL + p->filtersL[s].x1 - alpha * p->filtersL[s].y1;
            p->filtersL[s].x1 = stage_inL;
            p->filtersL[s].y1 = outL;
            stage_inL = outL;

            // Right Channel
            float outR = alpha * stage_inR + p->filtersR[s].x1 - alpha * p->filtersR[s].y1;
            p->filtersR[s].x1 = stage_inR;
            p->filtersR[s].y1 = outR;
            stage_inR = outR;
        }

        // Store current output for feedback in the next sample
        p->last_feedbackL = stage_inL;
        p->last_feedbackR = stage_inR;

        // Final mix: (Dry * DryAmt) + (Processed * Amount)
        p->ports[PHASER_OUT_L][i] = (inL * dry) + (stage_inL * amount);
        p->ports[PHASER_OUT_R][i] = (inR * dry) + (stage_inR * amount);
    }
}

void cleanupPhaser(LADSPA_Handle Instance) { free(Instance); }

static LADSPA_Descriptor * g_desc = NULL;
const LADSPA_Descriptor * ladspa_descriptor(unsigned long Index) {
    if (Index != 0) return NULL;
    if (!g_desc) {
        g_desc = (LADSPA_Descriptor *)calloc(1, sizeof(LADSPA_Descriptor));
        g_desc->UniqueID = 604001;
        g_desc->Label = strdup("mire_phaser_static");
        g_desc->Name = strdup("Mire Static Phaser ");
        g_desc->Maker = strdup("Mire");
        g_desc->PortCount = 9;

        LADSPA_PortDescriptor * pd = (LADSPA_PortDescriptor *)calloc(9, sizeof(LADSPA_PortDescriptor));
        for(int i=0; i<5; i++) pd[i] = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        pd[5]=pd[6] = LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO;
        pd[7]=pd[8] = LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO;
        g_desc->PortDescriptors = pd;

        const char ** names = (const char **)calloc(9, sizeof(char *));
        names[0]="Center Freq"; names[1]="Feedback"; names[2]="Stages"; 
        names[3]="Wet"; names[4]="Dry";
        names[5]="In L"; names[6]="In R"; names[7]="Out L"; names[8]="Out R";
        g_desc->PortNames = names;

        LADSPA_PortRangeHint * h = (LADSPA_PortRangeHint *)calloc(9, sizeof(LADSPA_PortRangeHint));
        // Freq: 20Hz to 10kHz
        h[0].LowerBound = 20; h[0].UpperBound = 10000; h[0].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_LOGARITHMIC | LADSPA_HINT_DEFAULT_MIDDLE;
        // Feedback: -0.99 to 0.99
        h[1].LowerBound = -0.5; h[1].UpperBound = 0.5; h[1].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_DEFAULT_MIDDLE;
        // Stages: 1 to 12
        h[2].LowerBound = 1; h[2].UpperBound = 12; h[2].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_INTEGER | LADSPA_HINT_DEFAULT_MAXIMUM;
        // Amount/Dry: 0 to 4
        h[3].LowerBound = 0; h[3].UpperBound = 4; h[3].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_DEFAULT_MIDDLE;
        h[4].LowerBound = 0; h[4].UpperBound = 4; h[4].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_DEFAULT_MINIMUM;
        g_desc->PortRangeHints = h;

        g_desc->instantiate = instantiatePhaser; g_desc->connect_port = connectPortPhaser; g_desc->run = runPhaser; g_desc->cleanup = cleanupPhaser;
    }
    return g_desc;
}
