#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <ladspa.h>

static volatile float mire_goa_bus[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

#define DY_MODE    0
#define DY_CHAN    1
#define DY_THRESH  2
#define DY_F_OUT   3
#define DY_F_IN    4
#define DY_DEPTH   5
#define DY_IN_L    6
#define DY_IN_R    7
#define DY_OUT_L   8
#define DY_OUT_R   9

typedef struct {
    LADSPA_Data * ports[10];
    float current_env;
    int ducking; 
} MireDynamics;

LADSPA_Handle instantiateDyn(const LADSPA_Descriptor * Descriptor, unsigned long SR) {
    MireDynamics * p = (MireDynamics *)calloc(1, sizeof(MireDynamics));
    p->current_env = 1.0f;
    return (LADSPA_Handle)p;
}

void connectPortDyn(LADSPA_Handle Instance, unsigned long Port, LADSPA_Data * Data) {
    ((MireDynamics *)Instance)->ports[Port] = Data;
}

void runDyn(LADSPA_Handle Instance, unsigned long SampleCount) {
    MireDynamics * p = (MireDynamics *)Instance;
    int mode = (int)(*p->ports[DY_MODE] + 0.5f);
    int chan = (int)(*p->ports[DY_CHAN] + 0.5f) & 15;

    float thresh = *p->ports[DY_THRESH];
    float f_out = *p->ports[DY_F_OUT]; 
    float f_in  = *p->ports[DY_F_IN];
    float depth = *p->ports[DY_DEPTH];

    for (unsigned long i = 0; i < SampleCount; i++) {
        float inL = p->ports[DY_IN_L][i];
        float inR = p->ports[DY_IN_R][i];

        if (mode == 0) { // TRIGGER
            if (fabsf(inL) > thresh || fabsf(inR) > thresh) {
                mire_goa_bus[chan] = 1.0f;
            }
            p->ports[DY_OUT_L][i] = inL;
            p->ports[DY_OUT_R][i] = inR;
        } else { // DUCKER
            if (mire_goa_bus[chan] > 0.5f) {
                p->ducking = 1;
                mire_goa_bus[chan] = 0.0f; 
            }

            float target_env = (p->ducking) ? depth : 1.0f;
            float speed = (target_env < p->current_env) ? f_out : f_in;
            
            p->current_env += (target_env - p->current_env) * speed;
            
            
            if (p->ducking && p->current_env <= depth + 0.005f) {
                p->ducking = 0;
            }

            p->ports[DY_OUT_L][i] = inL * p->current_env;
            p->ports[DY_OUT_R][i] = inR * p->current_env;
        }
    }
}

void cleanupDyn(LADSPA_Handle Instance) { free(Instance); }



static LADSPA_Descriptor * g_desc = NULL;
const LADSPA_Descriptor * ladspa_descriptor(unsigned long Index) {
    if (Index != 0) return NULL;
    if (!g_desc) {
        g_desc = (LADSPA_Descriptor *)calloc(1, sizeof(LADSPA_Descriptor));
        g_desc->UniqueID = 604006;
        g_desc->Label = strdup("mire_ducker");
        g_desc->Name = strdup("Mire Ducker");
        g_desc->Maker = strdup("Mire");
        g_desc->PortCount = 10;

        LADSPA_PortDescriptor * pd = (LADSPA_PortDescriptor *)calloc(10, sizeof(LADSPA_PortDescriptor));
        for(int i=0; i<6; i++) pd[i] = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        pd[6]=pd[7] = LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO;
        pd[8]=pd[9] = LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO;
        g_desc->PortDescriptors = pd;

        const char ** names = (const char **)calloc(10, sizeof(char *));
        names[0]="Mode (0:Trig/1:Duck)"; names[1]="Goa-Channel"; names[2]="Threshold"; 
        names[3]="Attack Speed"; names[4]="Release Speed"; names[5]="Depth";
        names[6]="In L"; names[7]="In R"; names[8]="Out L"; names[9]="Out R";
        g_desc->PortNames = names;

        LADSPA_PortRangeHint * h = (LADSPA_PortRangeHint *)calloc(10, sizeof(LADSPA_PortRangeHint));
        h[0].LowerBound=0; h[0].UpperBound=1; h[0].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_INTEGER|LADSPA_HINT_DEFAULT_MINIMUM;
        h[1].LowerBound=0; h[1].UpperBound=15; h[1].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_INTEGER|LADSPA_HINT_DEFAULT_MINIMUM;
        h[2].LowerBound=0; h[2].UpperBound=1; h[2].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MINIMUM;
        h[3].LowerBound=0.0001; h[3].UpperBound=0.09; h[3].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_DEFAULT_MIDDLE;;
        h[4].LowerBound=0.0001; h[4].UpperBound=0.09; h[4].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_DEFAULT_MINIMUM;;
        h[5].LowerBound=0; h[5].UpperBound=1; h[5].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_DEFAULT_MINIMUM;
        g_desc->PortRangeHints = h;

        g_desc->instantiate = instantiateDyn; g_desc->connect_port = connectPortDyn; g_desc->run = runDyn; g_desc->cleanup = cleanupDyn;
    }
    return g_desc;
}
