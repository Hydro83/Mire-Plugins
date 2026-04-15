#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <ladspa.h>

#define VO_FREQ_SHIFT 0
#define VO_MIX        1
#define VO_INPUT      2
#define VO_OUTPUT     3
#define BANDS         24

typedef struct {
    float b0, b1, b2, a1, a2;
    float w1, w2; 
    float band_gain; 
} Biquad;

typedef struct {
    LADSPA_Data * ports[4];
    Biquad m_filters[BANDS];
    float m_env[BANDS];
    float m_sr;
    float m_fLastShift;
} Vocoder;

static inline float fast_limit(float x) {
    if (x > 1.0f) return 1.0f;
    if (x < -1.0f) return -1.0f;
    return x - (0.3333333f * x * x * x);
}

void setup_vocoder_filter(Biquad *f, float freq, float sr, int band_index) {
    float q = 14.0f; 
    if (freq > sr * 0.45f) freq = sr * 0.45f;
    if (freq < 40.0f) freq = 40.0f;

    float omega = 2.0f * M_PI * freq / sr;
    float sn = sinf(omega);
    float cs = cosf(omega);
    float alpha = sn / (2.0f * q);
    
    float a0 = 1.0f + alpha;
    f->b0 = alpha / a0;
    f->b1 = 0.0f;
    f->b2 = -alpha / a0;
    f->a1 = (-2.0f * cs) / a0;
    f->a2 = (1.0f - alpha) / a0;

    float dampening = 1.0f / (1.0f + (float)band_index * 0.1f);
    f->band_gain = 12.0f * dampening; 
}

LADSPA_Handle instantiateVocoder(const LADSPA_Descriptor * Descriptor, unsigned long SampleRate) {
    Vocoder * p = (Vocoder *)calloc(1, sizeof(Vocoder));
    if (p) {
        p->m_sr = (float)SampleRate;
        p->m_fLastShift = -1.0f;
    }
    return (LADSPA_Handle)p;
}

void connectPortVocoder(LADSPA_Handle Instance, unsigned long Port, LADSPA_Data * Data) {
    Vocoder * p = (Vocoder *)Instance;
    if (p) p->ports[Port] = Data;
}

void runVocoder(LADSPA_Handle Instance, unsigned long SampleCount) {
    Vocoder * p = (Vocoder *)Instance;
    
    float currentShift = *p->ports[VO_FREQ_SHIFT];
    if (currentShift != p->m_fLastShift) {
        for (int i = 0; i < BANDS; i++) {
            float baseFreq = 60.0f * powf(1.25f, (float)i * 1.4f);
            setup_vocoder_filter(&p->m_filters[i], baseFreq * currentShift, p->m_sr, i);
        }
        p->m_fLastShift = currentShift;
    }

    const float release = 0.992f; 
    const float wet = *p->ports[VO_MIX];
    const float dry = 1.0f - wet;

    for (unsigned long i = 0; i < SampleCount; i++) {
        const float in = p->ports[VO_INPUT][i];
        float vocoded_sum = 0.0f;

        for (int b = 0; b < BANDS; b++) {
            Biquad *f = &p->m_filters[b];
            
            float w0 = in - f->a1 * f->w1 - f->a2 * f->w2;
            float band_sample = f->b0 * w0 + f->b1 * f->w1 + f->b2 * f->w2;
            f->w2 = f->w1; f->w1 = w0;

            float abs_s = fabsf(band_sample);
            if (abs_s > p->m_env[b]) p->m_env[b] = abs_s;
            else p->m_env[b] *= release;

            vocoded_sum += band_sample * p->m_env[b] * f->band_gain;
        }
        
        p->ports[VO_OUTPUT][i] = (in * dry) + (fast_limit(vocoded_sum) * wet);
    }
}

void cleanupVocoder(LADSPA_Handle Instance) { free(Instance); }

static LADSPA_Descriptor * g_desc = NULL;

const LADSPA_Descriptor * ladspa_descriptor(unsigned long Index) {
    if (Index != 0) return NULL;
    if (!g_desc) {
        g_desc = (LADSPA_Descriptor *)calloc(1, sizeof(LADSPA_Descriptor));
        g_desc->UniqueID = 604015; 
        g_desc->Label = strdup("mire_vocoder");
        g_desc->Name = strdup("Mire Vocoder");
        g_desc->Maker = strdup("Mire");
        g_desc->PortCount = 4;

        LADSPA_PortDescriptor * pd = (LADSPA_PortDescriptor *)calloc(4, sizeof(LADSPA_PortDescriptor));
        pd[VO_FREQ_SHIFT] = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        pd[VO_MIX]        = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        pd[VO_INPUT]      = LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO;
        pd[VO_OUTPUT]     = LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO;
        g_desc->PortDescriptors = pd;

        const char ** names = (const char **)calloc(4, sizeof(char *));
        names[VO_FREQ_SHIFT] = "Spectral Shift"; 
        names[VO_MIX]        = "Dry/Wet"; 
        names[VO_INPUT]      = "In"; 
        names[VO_OUTPUT]     = "Out";
        g_desc->PortNames = names;

        LADSPA_PortRangeHint * h = (LADSPA_PortRangeHint *)calloc(4, sizeof(LADSPA_PortRangeHint));
        h[VO_FREQ_SHIFT].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_DEFAULT_LOW;
        h[VO_FREQ_SHIFT].LowerBound = 0.5; h[VO_FREQ_SHIFT].UpperBound = 4.0;
        
        h[VO_MIX].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_DEFAULT_1;
        h[VO_MIX].LowerBound = 0.0; h[VO_MIX].UpperBound = 1.0;
        
        g_desc->PortRangeHints = h;

        g_desc->instantiate = instantiateVocoder;
        g_desc->connect_port = connectPortVocoder;
        g_desc->run = runVocoder;
        g_desc->cleanup = cleanupVocoder;
    }
    return g_desc;
}
