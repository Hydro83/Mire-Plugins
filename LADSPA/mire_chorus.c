#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <ladspa.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SYM_DEPTH    0
#define SYM_RATE     1
#define SYM_IN_L     2
#define SYM_IN_R     3
#define SYM_OUT_L    4
#define SYM_OUT_R    5
#define SYM_DRY_WET  6
#define SYM_VOICES   7

#define MAX_DELAY_SAMPLES 9600 
#define MAX_VOICES 16

typedef struct {
    LADSPA_Data * ports[8]; 
    float * m_pfBufferL;
    float * m_pfBufferR;
    unsigned long m_iBufferSize;
    unsigned long m_iWritePtr;
    
    float m_fPhases[MAX_VOICES];
    float m_fSampleRate;
} Symphonic;

float mire_read_buf(float *buf, float delay_samples, unsigned long write_ptr, unsigned long size) {
    float read_ptr = (float)write_ptr - delay_samples;
    while (read_ptr < 0) read_ptr += size;
    unsigned long i1 = (unsigned long)read_ptr % size;
    unsigned long i2 = (i1 + 1) % size;
    float frac = read_ptr - (unsigned long)read_ptr;
    return buf[i1] + frac * (buf[i2] - buf[i1]);
}

LADSPA_Handle instantiateSymphonic(const LADSPA_Descriptor * Descriptor, unsigned long SampleRate) {
    Symphonic * p = (Symphonic *)calloc(1, sizeof(Symphonic));
    p->m_fSampleRate = (float)SampleRate;
    p->m_iBufferSize = MAX_DELAY_SAMPLES;
    p->m_pfBufferL = (float *)calloc(p->m_iBufferSize, sizeof(float));
    p->m_pfBufferR = (float *)calloc(p->m_iBufferSize, sizeof(float));
    
    for(int i=0; i<MAX_VOICES; i++) {
        p->m_fPhases[i] = (2.0f * M_PI * i) / MAX_VOICES;
    }
    return (LADSPA_Handle)p;
}

void connectPortSymphonic(LADSPA_Handle Instance, unsigned long Port, LADSPA_Data * Data) {
    ((Symphonic *)Instance)->ports[Port] = Data;
}

void runSymphonic(LADSPA_Handle Instance, unsigned long SampleCount) {
    Symphonic * p = (Symphonic *)Instance;

    float depth_samples = (*p->ports[SYM_DEPTH] * 0.001f) * p->m_fSampleRate;
    float rate = *p->ports[SYM_RATE];
    float mix = *p->ports[SYM_DRY_WET];
    int num_voices = (int)(*p->ports[SYM_VOICES] + 0.5f);
    if (num_voices < 2) num_voices = 2;
    if (num_voices > MAX_VOICES) num_voices = MAX_VOICES;
    
    float base_delay = (0.015f * p->m_fSampleRate); 
    float phase_inc = (2.0f * M_PI * rate) / p->m_fSampleRate;

    for (unsigned long i = 0; i < SampleCount; i++) {
        p->m_pfBufferL[p->m_iWritePtr] = p->ports[SYM_IN_L][i];
        p->m_pfBufferR[p->m_iWritePtr] = p->ports[SYM_IN_R][i];

        float sumL = 0, sumR = 0;

        for (int v = 0; v < num_voices; v++) {
            float mod = sinf(p->m_fPhases[v]);
            float delay = base_delay + (mod * depth_samples);
            
            float voice_out;
            if (v % 2 == 0) {
                voice_out = mire_read_buf(p->m_pfBufferL, delay, p->m_iWritePtr, p->m_iBufferSize);
                sumL += voice_out;
            } else {
                voice_out = mire_read_buf(p->m_pfBufferR, delay, p->m_iWritePtr, p->m_iBufferSize);
                sumR += voice_out;
            }

            p->m_fPhases[v] += phase_inc;
            if (p->m_fPhases[v] >= 2.0f * M_PI) p->m_fPhases[v] -= 2.0f * M_PI;
        }

        float wetL = sumL / (num_voices / 2.0f);
        float wetR = sumR / (num_voices / 2.0f);

        p->ports[SYM_OUT_L][i] = (p->ports[SYM_IN_L][i] * (1.0f - mix)) + (wetL * mix);
        p->ports[SYM_OUT_R][i] = (p->ports[SYM_IN_R][i] * (1.0f - mix)) + (wetR * mix);

        p->m_iWritePtr = (p->m_iWritePtr + 1) % p->m_iBufferSize;
    }
}

void cleanupSymphonic(LADSPA_Handle Instance) {
    Symphonic * p = (Symphonic *)Instance;
    free(p->m_pfBufferL);
    free(p->m_pfBufferR);
    free(p);
}

static LADSPA_Descriptor * g_desc = NULL;
const LADSPA_Descriptor * ladspa_descriptor(unsigned long Index) {
    if (Index != 0) return NULL;
    if (!g_desc) {
        g_desc = (LADSPA_Descriptor *)calloc(1, sizeof(LADSPA_Descriptor));
        g_desc->UniqueID = 604002;
        g_desc->Label = strdup("mire_chorus");
        g_desc->Name = strdup("Mire Chorus");
        g_desc->Maker = strdup("Mire");
        g_desc->PortCount = 8;

        LADSPA_PortDescriptor * pd = (LADSPA_PortDescriptor *)calloc(8, sizeof(LADSPA_PortDescriptor));
        pd[0]=pd[1]=pd[6]=pd[7] = LADSPA_PORT_INPUT|LADSPA_PORT_CONTROL;
        pd[2]=pd[3] = LADSPA_PORT_INPUT|LADSPA_PORT_AUDIO;
        pd[4]=pd[5] = LADSPA_PORT_OUTPUT|LADSPA_PORT_AUDIO;
        g_desc->PortDescriptors = pd;

        const char ** names = (const char **)calloc(8, sizeof(char *));
        names[0]="Mod Depth (ms)"; names[1]="Mod Rate (Hz)";
        names[2]="In L"; names[3]="In R"; names[4]="Out L"; names[5]="Out R";
        names[6]="Dry/Wet Mix"; names[7]="Number of Voices";
        g_desc->PortNames = names;

        LADSPA_PortRangeHint * h = (LADSPA_PortRangeHint *)calloc(8, sizeof(LADSPA_PortRangeHint));
        h[0].LowerBound = 0; h[0].UpperBound = 10; h[0].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MIDDLE;
        h[1].LowerBound = 0.1; h[1].UpperBound = 5; h[1].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MINIMUM;
        h[6].LowerBound = 0; h[6].UpperBound = 1; h[6].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MAXIMUM;
        h[7].LowerBound = 2; h[7].UpperBound = 16; h[7].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_INTEGER|LADSPA_HINT_DEFAULT_MAXIMUM;
        g_desc->PortRangeHints = h;

        g_desc->instantiate = instantiateSymphonic; 
        g_desc->connect_port = connectPortSymphonic; 
        g_desc->run = runSymphonic; 
        g_desc->cleanup = cleanupSymphonic;
    }
    return g_desc;
}
