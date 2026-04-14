#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <ladspa.h>

#define VO_FREQ_SHIFT 0
#define VO_INPUT      1
#define VO_OUTPUT     2
#define BANDS         24

typedef struct {
    float b0, b1, b2, a1, a2;
    float x1, x2, y1, y2;
} Biquad;

typedef struct {
    LADSPA_Data * m_pfFreqShift;
    LADSPA_Data * m_pfInput;
    LADSPA_Data * m_pfOutput;
    Biquad m_filters[BANDS];
    float m_env[BANDS];
    float m_sr;
    float m_fLastShift;
} SciFiVocoder;

// Helper to clip signals to prevent ear-piercing peaks
static inline float soft_clip(float x) {
    if (x > 1.0f) return 1.0f;
    if (x < -1.0f) return -1.0f;
    return x * (1.5f - 0.5f * x * x); // Simple cubic soft saturator
}

void setup_sci_filter(Biquad *f, float freq, float sr) {
    float q = 14.0f; // Slightly lowered Q to reduce "whistle" intensity
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
}

LADSPA_Handle instantiateSciFi(const LADSPA_Descriptor * Descriptor, unsigned long SampleRate) {
    SciFiVocoder * p = (SciFiVocoder *)calloc(1, sizeof(SciFiVocoder));
    p->m_sr = (float)SampleRate;
    p->m_fLastShift = -1.0f;
    return (LADSPA_Handle)p;
}

void connectPortSciFi(LADSPA_Handle Instance, unsigned long Port, LADSPA_Data * Data) {
    SciFiVocoder * p = (SciFiVocoder *)Instance;
    switch (Port) {
        case VO_FREQ_SHIFT: p->m_pfFreqShift = Data; break;
        case VO_INPUT:      p->m_pfInput = Data; break;
        case VO_OUTPUT:     p->m_pfOutput = Data; break;
    }
}

void runSciFi(LADSPA_Handle Instance, unsigned long SampleCount) {
    SciFiVocoder * p = (SciFiVocoder *)Instance;
    
    float currentShift = *p->m_pfFreqShift;
    if (currentShift != p->m_fLastShift) {
        for (int i = 0; i < BANDS; i++) {
            float baseFreq = 60.0f * powf(1.25f, (float)i * 1.4f);
            setup_sci_filter(&p->m_filters[i], baseFreq * currentShift, p->m_sr);
        }
        p->m_fLastShift = currentShift;
    }

    float release = 0.992f; 
    float gain_multiplier = 12.0f; // Reduced base gain

    for (unsigned long i = 0; i < SampleCount; i++) {
        float in = p->m_pfInput[i];
        float mixed_out = 0.0f;

        for (int b = 0; b < BANDS; b++) {
            Biquad *f = &p->m_filters[b];
            
            float band_sample = f->b0*in + f->b1*f->x1 + f->b2*f->x2 - f->a1*f->y1 - f->a2*f->y2;
            f->x2 = f->x1; f->x1 = in;
            f->y2 = f->y1; f->y1 = band_sample;

            // Envelope following
            float abs_s = fabsf(band_sample);
            if (abs_s > p->m_env[b]) p->m_env[b] = abs_s;
            else p->m_env[b] *= release;

            // INTERNAL LIMITER: prevent any single band from dominating
            float band_final = band_sample * p->m_env[b] * gain_multiplier;
            
            // Apply a "high-frequency dampener" so higher bands aren't as loud
            float dampening = 1.0f / (1.0f + (float)b * 0.1f);
            mixed_out += soft_clip(band_final * dampening);
        }
        
        // Final Output safety clip
        p->m_pfOutput[i] = soft_clip(mixed_out);
    }
}

void cleanupSciFi(LADSPA_Handle Instance) { free(Instance); }

static LADSPA_Descriptor * g_desc = NULL;

const LADSPA_Descriptor * ladspa_descriptor(unsigned long Index) {
    if (Index != 0) return NULL;
    if (!g_desc) {
        g_desc = (LADSPA_Descriptor *)calloc(1, sizeof(LADSPA_Descriptor));
        g_desc->UniqueID = 604016; 
        g_desc->Label = strdup("mire_vocoder");
        g_desc->Name = strdup("Mire Vocoder");
        g_desc->Maker = strdup("Mire");
        g_desc->PortCount = 3;

        LADSPA_PortDescriptor * pd = (LADSPA_PortDescriptor *)calloc(3, sizeof(LADSPA_PortDescriptor));
        pd[0] = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        pd[1] = LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO;
        pd[2] = LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO;
        g_desc->PortDescriptors = pd;

        const char ** names = (const char **)calloc(3, sizeof(char *));
        names[0] = "Spectral Shift"; names[1] = "In"; names[2] = "Out";
        g_desc->PortNames = names;

        LADSPA_PortRangeHint * h = (LADSPA_PortRangeHint *)calloc(3, sizeof(LADSPA_PortRangeHint));
        h[0].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_DEFAULT_LOW;
        h[0].LowerBound = 0.5; h[0].UpperBound = 4.0;
        h[1].HintDescriptor = 0; h[2].HintDescriptor = 0;
        g_desc->PortRangeHints = h;

        g_desc->instantiate = instantiateSciFi;
        g_desc->connect_port = connectPortSciFi;
        g_desc->run = runSciFi;
        g_desc->cleanup = cleanupSciFi;
    }
    return g_desc;
}
