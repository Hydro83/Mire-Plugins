#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <ladspa.h>

#define LO_BITS_L   0
#define LO_RATE_L   1
#define LO_BITS_R   2
#define LO_RATE_R   3
#define LO_LINK     4
#define LO_MONO     5
#define LO_IN_L     6
#define LO_IN_R     7
#define LO_OUT_L    8
#define LO_OUT_R    9

typedef struct {
    LADSPA_Data * ports[10];
    float m_fLastL, m_fLastR;
    int m_iCountL, m_iCountR;
} LoFi;

LADSPA_Handle instantiateLoFi(const LADSPA_Descriptor * Descriptor, unsigned long SampleRate) {
    LoFi * p = (LoFi *)calloc(1, sizeof(LoFi));
    return (LADSPA_Handle)p;
}

void connectPortLoFi(LADSPA_Handle Instance, unsigned long Port, LADSPA_Data * Data) {
    ((LoFi *)Instance)->ports[Port] = Data;
}

void runLoFi(LADSPA_Handle Instance, unsigned long SampleCount) {
    LoFi * p = (LoFi *)Instance;
    
    float bitsL = *p->ports[LO_BITS_L];
    float rateL = *p->ports[LO_RATE_L];
    
    int linked = (*p->ports[LO_LINK] > 0.5f);
    float bitsR = linked ? bitsL : *p->ports[LO_BITS_R];
    float rateR = linked ? rateL : *p->ports[LO_RATE_R];
    
    int monoSum = (*p->ports[LO_MONO] > 0.5f);

    float levL = powf(2.0f, bitsL);
    float levR = powf(2.0f, bitsR);
    
    int skipL = (int)rateL;
    int skipR = (int)rateR;
    if (skipL < 1) skipL = 1;
    if (skipR < 1) skipR = 1;

    for (unsigned long i = 0; i < SampleCount; i++) {
        p->m_iCountL++;
        if (p->m_iCountL >= skipL) {
            float sL = p->ports[LO_IN_L][i];
            p->m_fLastL = roundf(sL * levL) / levL;
            p->m_iCountL = 0;
        }

        p->m_iCountR++;
        if (p->m_iCountR >= skipR) {
            float sR = p->ports[LO_IN_R][i];
            p->m_fLastR = roundf(sR * levR) / levR;
            p->m_iCountR = 0;
        }

        float outL = p->m_fLastL;
        float outR = p->m_fLastR;

        if (monoSum) {
            float mixed = (outL + outR) * 0.5f;
            outL = outR = mixed;
        }

        p->ports[LO_OUT_L][i] = outL;
        p->ports[LO_OUT_R][i] = outR;
    }
}

void cleanupLoFi(LADSPA_Handle Instance) {
    free(Instance);
}

static LADSPA_Descriptor * g_desc = NULL;
const LADSPA_Descriptor * ladspa_descriptor(unsigned long Index) {
    if (Index != 0) return NULL;
    if (!g_desc) {
        g_desc = (LADSPA_Descriptor *)calloc(1, sizeof(LADSPA_Descriptor));
        g_desc->UniqueID = 604009;
        g_desc->Label = strdup("mire_lofi");
        g_desc->Name = strdup("Mire LoFi");
        g_desc->Maker = strdup("Mire");
        g_desc->PortCount = 10;
        
        LADSPA_PortDescriptor * pd = (LADSPA_PortDescriptor *)calloc(10, sizeof(LADSPA_PortDescriptor));
        for(int i=0; i<6; i++) pd[i] = LADSPA_PORT_INPUT|LADSPA_PORT_CONTROL;
        pd[6]=pd[7] = LADSPA_PORT_INPUT|LADSPA_PORT_AUDIO;
        pd[8]=pd[9] = LADSPA_PORT_OUTPUT|LADSPA_PORT_AUDIO;
        g_desc->PortDescriptors = pd;

        const char ** names = (const char **)calloc(10, sizeof(char *));
        names[0]="Bits L"; names[1]="Rate L"; names[2]="Bits R"; names[3]="Rate R";
        names[4]="Link L"; names[5]="Mono Switch"; 
        names[6]="In L"; names[7]="In R"; names[8]="Out L"; names[9]="Out R";
        g_desc->PortNames = names;

        LADSPA_PortRangeHint * h = (LADSPA_PortRangeHint *)calloc(10, sizeof(LADSPA_PortRangeHint));
        
        
        h[0].LowerBound = h[2].LowerBound = 1; h[0].UpperBound = h[2].UpperBound = 16;
        h[0].HintDescriptor = h[2].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_HIGH;
        h[1].LowerBound = h[3].LowerBound = 1; h[1].UpperBound = h[3].UpperBound = 50;
        h[1].HintDescriptor = h[3].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_LOW;
        h[4].HintDescriptor = LADSPA_HINT_TOGGLED|LADSPA_HINT_DEFAULT_1;
        h[5].HintDescriptor = LADSPA_HINT_TOGGLED|LADSPA_HINT_DEFAULT_0;
        g_desc->PortRangeHints = h;
        g_desc->instantiate = instantiateLoFi; 
        g_desc->connect_port = connectPortLoFi; 
        g_desc->run = runLoFi; 
        g_desc->cleanup = cleanupLoFi;
    }
    return g_desc;
}
