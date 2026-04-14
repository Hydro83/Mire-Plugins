#include <stdlib.h>
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
    p->sample_rate = (float)SR;
    // Max delay of 0.1s is plenty for resonators
    p->buf_size = SR * 0.1; 
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

    // Calculate delay offset based on frequency (Delay = SR / Freq)
    // A 440Hz note = 100 samples delay (at 44.1k)
    float delay_samples = p->sample_rate / (freq < 10.0f ? 10.0f : freq);

    for (unsigned long i = 0; i < SampleCount; i++) {
        float input = p->ports[RES_INPUT][i];
        
        // Find read position
        float read_ptr = (float)p->write_ptr - delay_samples;
        while (read_ptr < 0) read_ptr += p->buf_size;
        
        // Simple linear interpolation for smoother tuning
        int i_part = (int)read_ptr;
        float f_part = read_ptr - i_part;
        int next_i = (i_part + 1) % p->buf_size;
        float delayed_sample = p->buffer[i_part] * (1.0f - f_part) + p->buffer[next_i] * f_part;

        // Feed it back
        p->buffer[p->write_ptr] = input + (delayed_sample * feedback);
        
        // Mix dry/wet
        p->ports[RES_OUTPUT][i] = (input * (1.0f - drywet)) + (delayed_sample * drywet);

        p->write_ptr = (p->write_ptr + 1) % p->buf_size;
    }
}

void cleanupRes(LADSPA_Handle Instance) {
    MireResonator * p = (MireResonator *)Instance;
    free(p->buffer);
    free(p);
}

static LADSPA_Descriptor * g_desc = NULL;

const LADSPA_Descriptor * ladspa_descriptor(unsigned long Index) {
    if (Index != 0) return NULL;
    if (!g_desc) {
        g_desc = (LADSPA_Descriptor *)malloc(sizeof(LADSPA_Descriptor));
        g_desc->UniqueID = 604014;
        g_desc->Label = "mire_resonator";
        g_desc->Properties = LADSPA_PROPERTY_REALTIME;
        g_desc->Name = "Mire Resonator";
        g_desc->Maker = "Mire";
        g_desc->PortCount = 5;

        LADSPA_PortDescriptor * pd = (LADSPA_PortDescriptor *)calloc(5, sizeof(LADSPA_PortDescriptor));
        pd[0]=pd[1]=pd[2] = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        pd[3] = LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO;
        pd[4] = LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO;
        g_desc->PortDescriptors = pd;

        const char ** names = (const char **)calloc(5, sizeof(char *));
        names[0]="Frequency (Hz)"; names[1]="Feedback (0-1)"; names[2]="Dry/Wet";
        names[3]="Input"; names[4]="Output";
        g_desc->PortNames = names;

        LADSPA_PortRangeHint * h = (LADSPA_PortRangeHint *)calloc(5, sizeof(LADSPA_PortRangeHint));
        h[0].LowerBound=20; h[0].UpperBound=5000; h[0].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_LOGARITHMIC|LADSPA_HINT_DEFAULT_MIDDLE;
        h[1].LowerBound=0; h[1].UpperBound=0.99; h[1].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MIDDLE;
        h[2].LowerBound=0; h[2].UpperBound=1; h[2].HintDescriptor=LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MAXIMUM;
        g_desc->PortRangeHints = h;

        g_desc->instantiate = instantiateRes; g_desc->connect_port = connectPortRes; g_desc->run = runRes; g_desc->cleanup = cleanupRes;
    }
    return g_desc;
}
