#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <ladspa.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SYM_DEPTH  0
#define SYM_RATE   1
#define SYM_IN_L   2
#define SYM_IN_R   3
#define SYM_OUT_L  4
#define SYM_OUT_R  5

#define MAX_DELAY_SAMPLES 9600 // 200ms at 48kHz

typedef struct {
    LADSPA_Data * m_pfDepth, * m_pfRate;
    LADSPA_Data * m_pfInL, * m_pfInR, * m_pfOutL, * m_pfOutR;
    
    float * m_pfBufferL;
    float * m_pfBufferR;
    unsigned long m_iBufferSize;
    unsigned long m_iWritePtr;
    
    float m_fPhase1, m_fPhase2, m_fPhase3;
    float m_fSampleRate;
} Symphonic;

// Linear Interpolation for smooth pitch modulation
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
    if (p) {
        p->m_fSampleRate = (float)SampleRate;
        p->m_iBufferSize = MAX_DELAY_SAMPLES;
        p->m_pfBufferL = (float *)calloc(p->m_iBufferSize, sizeof(float));
        p->m_pfBufferR = (float *)calloc(p->m_iBufferSize, sizeof(float));
        p->m_iWritePtr = 0;
        
        // 120 degree phase offsets
        p->m_fPhase1 = 0.0f;
        p->m_fPhase2 = 2.094395f; 
        p->m_fPhase3 = 4.188790f;
    }
    return (LADSPA_Handle)p;
}

void connectPortSymphonic(LADSPA_Handle Instance, unsigned long Port, LADSPA_Data * Data) {
    Symphonic * p = (Symphonic *)Instance;
    if (!p) return;
    switch (Port) {
        case SYM_DEPTH: p->m_pfDepth = Data; break;
        case SYM_RATE:  p->m_pfRate = Data;  break;
        case SYM_IN_L:  p->m_pfInL = Data;   break;
        case SYM_IN_R:  p->m_pfInR = Data;   break;
        case SYM_OUT_L: p->m_pfOutL = Data;  break;
        case SYM_OUT_R: p->m_pfOutR = Data;  break;
    }
}

void runSymphonic(LADSPA_Handle Instance, unsigned long SampleCount) {
    Symphonic * p = (Symphonic *)Instance;
    if (!p) return;

    float depth_samples = (*p->m_pfDepth * 0.001f) * p->m_fSampleRate;
    float rate = *p->m_pfRate;
    float base_delay = depth_samples + (0.002f * p->m_fSampleRate); // 30ms base offset
    float phase_inc = (2.0f * M_PI * rate) / p->m_fSampleRate;

    for (unsigned long i = 0; i < SampleCount; i++) {
        p->m_pfBufferL[p->m_iWritePtr] = p->m_pfInL[i];
        p->m_pfBufferR[p->m_iWritePtr] = p->m_pfInR[i];

        // Three modulated voices
        float mod1 = sinf(p->m_fPhase1);
        float mod2 = sinf(p->m_fPhase2 * 1.03f); // Slight variations
        float mod3 = sinf(p->m_fPhase3 * 0.97f);

        // Read voices from buffer (Left channel input used for the thickest modulation)
        float v1 = mire_read_buf(p->m_pfBufferL, base_delay + (mod1 * depth_samples), p->m_iWritePtr, p->m_iBufferSize);
        float v2 = mire_read_buf(p->m_pfBufferR, base_delay + (mod2 * depth_samples), p->m_iWritePtr, p->m_iBufferSize);
        float v3 = (v1 + v2) * 0.5f; // Center voice mix
        v3 = mire_read_buf(p->m_pfBufferL, base_delay + (mod3 * depth_samples), p->m_iWritePtr, p->m_iBufferSize);

        // Mix back to stereo: Voice 1 and 2 provide width, Voice 3 provides center stability
        p->m_pfOutL[i] = (v1 * 0.707f) + (v3 * 0.4f);
        p->m_pfOutR[i] = (v2 * 0.707f) + (v3 * 0.4f);

        p->m_fPhase1 += phase_inc;
        p->m_fPhase2 += phase_inc;
        p->m_fPhase3 += phase_inc;
        
        // Phase wrapping
        if (p->m_fPhase1 > 2.0f * M_PI) p->m_fPhase1 -= 2.0f * M_PI;
        if (p->m_fPhase2 > 2.0f * M_PI) p->m_fPhase2 -= 2.0f * M_PI;
        if (p->m_fPhase3 > 2.0f * M_PI) p->m_fPhase3 -= 2.0f * M_PI;

        p->m_iWritePtr = (p->m_iWritePtr + 1) % p->m_iBufferSize;
    }
}

void cleanupSymphonic(LADSPA_Handle Instance) {
    Symphonic * p = (Symphonic *)Instance;
    if (p) {
        if (p->m_pfBufferL) free(p->m_pfBufferL);
        if (p->m_pfBufferR) free(p->m_pfBufferR);
        free(p);
    }
}

static LADSPA_Descriptor * g_desc = NULL;

const LADSPA_Descriptor * ladspa_descriptor(unsigned long Index) {
    if (Index != 0) return NULL;
    if (!g_desc) {
        g_desc = (LADSPA_Descriptor *)calloc(1, sizeof(LADSPA_Descriptor));
        g_desc->UniqueID = 604004;
        g_desc->Label = strdup("mire_chorus");
        g_desc->Name = strdup("Mire Chorus");
        g_desc->Properties = LADSPA_PROPERTY_HARD_RT_CAPABLE;
        g_desc->Maker = strdup("Mire");
        g_desc->PortCount = 6;

        LADSPA_PortDescriptor * pd = (LADSPA_PortDescriptor *)calloc(6, sizeof(LADSPA_PortDescriptor));
        pd[0]=pd[1] = LADSPA_PORT_INPUT|LADSPA_PORT_CONTROL;
        pd[2]=pd[3] = LADSPA_PORT_INPUT|LADSPA_PORT_AUDIO;
        pd[4]=pd[5] = LADSPA_PORT_OUTPUT|LADSPA_PORT_AUDIO;
        g_desc->PortDescriptors = pd;

        const char ** names = (const char **)calloc(6, sizeof(char *));
        names[0]="Mod Depth (ms)"; names[1]="Mod Rate (Hz)";
        names[2]="In L"; names[3]="In R"; names[4]="Out L"; names[5]="Out R";
        g_desc->PortNames = names;

        LADSPA_PortRangeHint * h = (LADSPA_PortRangeHint *)calloc(6, sizeof(LADSPA_PortRangeHint));
        h[0].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_LOW;
        h[0].LowerBound = 0; h[0].UpperBound = 10;
        h[1].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_LOW;
        h[1].LowerBound = 0.1; h[1].UpperBound = 5;
        g_desc->PortRangeHints = h;

        g_desc->instantiate = instantiateSymphonic; 
        g_desc->connect_port = connectPortSymphonic; 
        g_desc->run = runSymphonic; 
        g_desc->cleanup = cleanupSymphonic;
    }
    return g_desc;
}
