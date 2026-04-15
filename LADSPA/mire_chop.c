#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <ladspa.h>

#define CHOP_BPM         0
#define CHOP_RATE_MODE   1
#define CHOP_FADE_IN     2
#define CHOP_FADE_OUT    3
#define CHOP_DRY_WET     4
#define CHOP_DIP         5
#define CHOP_IN_L        6
#define CHOP_IN_R        7
#define CHOP_OUT_L       8
#define CHOP_OUT_R       9

typedef struct {
    LADSPA_Data * ports[10];
    float sample_rate;
    float phase;
    float last_gain;
} MireChop;

LADSPA_Handle instantiateChop(const LADSPA_Descriptor * Descriptor, unsigned long SampleRate) {
    MireChop * p = (MireChop *)calloc(1, sizeof(MireChop));
    p->sample_rate = (float)SampleRate;
    p->phase = 0.0f;
    p->last_gain = 1.0f;
    return (LADSPA_Handle)p;
}

void connectPortChop(LADSPA_Handle Instance, unsigned long Port, LADSPA_Data * Data) {
    ((MireChop *)Instance)->ports[Port] = Data;
}

void runChop(LADSPA_Handle Instance, unsigned long SampleCount) {
    MireChop * p = (MireChop *)Instance;

    float bpm = *p->ports[CHOP_BPM];
    int mode = (int)(*p->ports[CHOP_RATE_MODE] + 0.5f);
    float fade_in = *p->ports[CHOP_FADE_IN];
    float fade_out = *p->ports[CHOP_FADE_OUT];
    float dry_wet = *p->ports[CHOP_DRY_WET];
    int dip_on_first = (int)(*p->ports[CHOP_DIP] + 0.5f);

    float division = 0.0f;
    switch(mode) {
        case 1: division = 1.0f;  break;
        case 2: division = 2.0f;  break;
        case 3: division = 4.0f;  break;
        case 4: division = 8.0f;  break;
        default: division = 0.0f; break;
    }

    float freq = (bpm / 60.0f) * division;
    float phase_inc = freq / p->sample_rate;

    for (unsigned long i = 0; i < SampleCount; i++) {
        float target_gain;
        
        if (division <= 0.0f) {
            target_gain = 1.0f;
        } else {
            if (dip_on_first) {
                target_gain = (p->phase < 0.5f) ? 0.0f : 1.0f;
            } else {
                target_gain = (p->phase < 0.5f) ? 1.0f : 0.0f;
            }
        }

        float in_speed = 0.0001f / (0.0001f + (fade_in * 0.2f));
        float out_speed = 0.0001f / (0.0001f + (fade_out * 0.2f));
        
        if (p->last_gain < target_gain) {
            p->last_gain += in_speed;
            if (p->last_gain > target_gain) p->last_gain = target_gain;
        } else if (p->last_gain > target_gain) {
            p->last_gain -= out_speed;
            if (p->last_gain < target_gain) p->last_gain = target_gain;
        }

        float inL = p->ports[CHOP_IN_L][i];
        float inR = p->ports[CHOP_IN_R][i];

        p->ports[CHOP_OUT_L][i] = (inL * p->last_gain * dry_wet) + (inL * (1.0f - dry_wet));
        p->ports[CHOP_OUT_R][i] = (inR * p->last_gain * dry_wet) + (inR * (1.0f - dry_wet));

        if (division > 0.0f) {
            p->phase += phase_inc;
            if (p->phase >= 1.0f) p->phase -= 1.0f;
        } else {
            p->phase = 0.0f;
        }
    }
}

void cleanupChop(LADSPA_Handle Instance) { free(Instance); }

static LADSPA_Descriptor * g_desc = NULL;
const LADSPA_Descriptor * ladspa_descriptor(unsigned long Index) {
    if (Index != 0) return NULL;
    if (!g_desc) {
        g_desc = (LADSPA_Descriptor *)calloc(1, sizeof(LADSPA_Descriptor));
        g_desc->UniqueID = 604001;
        g_desc->Label = strdup("mire_chop");
        g_desc->Name = strdup("Mire Chop");
        g_desc->Maker = strdup("Mire");
        g_desc->PortCount = 10;

        LADSPA_PortDescriptor * pd = (LADSPA_PortDescriptor *)calloc(10, sizeof(LADSPA_PortDescriptor));
        for(int i=0; i<6; i++) pd[i] = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        pd[6]=pd[7] = LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO;
        pd[8]=pd[9] = LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO;
        g_desc->PortDescriptors = pd;

        const char ** names = (const char **)calloc(10, sizeof(char *));
        names[0]="BPM"; 
        names[1]="Rate (0:Off, 1:1/4, 2:1/8, 3:1/16, 4:1/32)"; 
        names[2]="Fade In"; 
        names[3]="Fade Out"; 
        names[4]="Dry/Wet";
        names[5]="Dip on First (Sidechain)";
        names[6]="In L"; names[7]="In R"; 
        names[8]="Out L"; names[9]="Out R";
        g_desc->PortNames = names;

        LADSPA_PortRangeHint * h = (LADSPA_PortRangeHint *)calloc(10, sizeof(LADSPA_PortRangeHint));
        h[0].LowerBound = 60; h[0].UpperBound = 300; h[0].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_INTEGER | LADSPA_HINT_DEFAULT_NONE;
        h[1].LowerBound = 0; h[1].UpperBound = 4; h[1].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_INTEGER | LADSPA_HINT_DEFAULT_MIDDLE;
        h[2].LowerBound = 0; h[2].UpperBound = 1; h[2].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_DEFAULT_LOW;
        h[3].LowerBound = 0; h[3].UpperBound = 1; h[3].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_DEFAULT_LOW;
        h[4].LowerBound = 0; h[4].UpperBound = 1; h[4].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_DEFAULT_1;
        h[5].LowerBound = 0; h[5].UpperBound = 1; h[5].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_INTEGER | LADSPA_HINT_DEFAULT_0;
        
        g_desc->PortRangeHints = h;

        g_desc->instantiate = instantiateChop; 
        g_desc->connect_port = connectPortChop; 
        g_desc->run = runChop; 
        g_desc->cleanup = cleanupChop;
    }
    return g_desc;
}
