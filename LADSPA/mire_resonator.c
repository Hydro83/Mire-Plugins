#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <ladspa.h>

#define RES_FREQ    0
#define RES_FEED    1
#define RES_DRYWET  2
#define RES_INPUT   3
#define RES_OUTPUT  4

typedef struct {
    LADSPA_Data * ports[5];
    LADSPA_Data * buffer;
    unsigned long buf_size;
    unsigned long write_ptr;
    float sample_rate;
} MireResonator;

LADSPA_Handle instantiateRes(const LADSPA_Descriptor * Descriptor, unsigned long SR) {
    MireResonator * p = (MireResonator *)calloc(1, sizeof(MireResonator));
    if (!p) return NULL;
    p->sample_rate = (float)SR;
    p->buf_size = (unsigned long)(SR * 0.1); 
    p->buffer = (LADSPA_Data *)calloc(p->buf_size, sizeof(LADSPA_Data));
    return (LADSPA_Handle)p;
}

void connectPortRes(LADSPA_Handle Instance, unsigned long Port, LADSPA_Data * Data) {
    ((MireResonator *)Instance)->ports[Port] = Data;
}

void runRes(LADSPA_Handle Instance, unsigned long SampleCount) {
    MireResonator * p = (MireResonator *)Instance;
    
    float freq = *p->ports[RES_FREQ];
    float feedback = *p->ports[RES_FEED];
    float drywet = *p->ports[RES_DRYWET];

    if (freq < 20.0f) freq = 20.0f; 
    float delay_samples = p->sample_rate / freq;
    
    if (delay_samples > p->buf_size - 2) delay_samples = p->buf_size - 2;

    for (unsigned long i = 0; i < SampleCount; i++) {
        float input = p->ports[RES_INPUT][i];
        
        float read_pos = (float)p->write_ptr - delay_samples;
        while (read_pos < 0) read_pos += (float)p->buf_size;
        
        int i_part = (int)read_pos;
        float f_part = read_pos - (float)i_part;
        int next_i = (i_part + 1) % p->buf_size;
        
        float delayed_sample = p->buffer[i_part] * (1.0f - f_part) + p->buffer[next_i] * f_part;

        float val_to_write = input + (delayed_sample * feedback);
        if (fabsf(val_to_write) < 1e-15f) val_to_write = 0.0f;
        
        p->buffer[p->write_ptr] = val_to_write;
        
        p->ports[RES_OUTPUT][i] = (input * (1.0f - drywet)) + (delayed_sample * drywet);

        p->write_ptr = (p->write_ptr + 1) % p->buf_size;
    }
}

void cleanupRes(LADSPA_Handle Instance) {
    MireResonator * p = (MireResonator *)Instance;
    if (p->buffer) free(p->buffer);
    free(p);
}

static LADSPA_Descriptor * g_desc = NULL;
const LADSPA_Descriptor * ladspa_descriptor(unsigned long Index) {
    if (Index != 0) return NULL;
    if (!g_desc) {
        g_desc = (LADSPA_Descriptor *)calloc(1, sizeof(LADSPA_Descriptor));
        g_desc->UniqueID = 604012;
        g_desc->Label = strdup("mire_resonator");
        g_desc->Name = strdup("Mire Resonator");
        g_desc->Maker = strdup("Mire");
        g_desc->PortCount = 5;

        LADSPA_PortDescriptor * pd = (LADSPA_PortDescriptor *)calloc(5, sizeof(LADSPA_PortDescriptor));
        pd[0]=pd[1]=pd[2] = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        pd[3] = LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO;
        pd[4] = LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO;
        g_desc->PortDescriptors = pd;

        const char ** names = (const char **)calloc(5, sizeof(char *));
        names[0]="Frequency (Hz)"; names[1]="Feedback Amount"; names[2]="Dry/Wet Mix";
        names[3]="Input"; names[4]="Output";
        g_desc->PortNames = names;

        LADSPA_PortRangeHint * h = (LADSPA_PortRangeHint *)calloc(5, sizeof(LADSPA_PortRangeHint));
        
        h[0].LowerBound=20; h[0].UpperBound=5000; 
        h[0].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_LOGARITHMIC|LADSPA_HINT_DEFAULT_MIDDLE;
        h[1].LowerBound=0; h[1].UpperBound=0.999; 
        h[1].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_LOW;
        h[2].LowerBound=0; h[2].UpperBound=1; 
        h[2].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MIDDLE;
        
        g_desc->PortRangeHints = h;
        g_desc->instantiate = instantiateRes; 
        g_desc->connect_port = connectPortRes; 
        g_desc->run = runRes; 
        g_desc->cleanup = cleanupRes;
    }
    return g_desc;
}
