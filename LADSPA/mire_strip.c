#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <ladspa.h>

#define M_CRANK  0
#define M_SLAM   1
#define M_HIGH   2
#define M_MID    3
#define M_LOW    4
#define M_LEVEL  5
#define M_HPF    6
#define M_INPUT  7
#define M_OUTPUT 8

typedef struct {
    LADSPA_Data * m_pfCrank, * m_pfSlam, * m_pfHigh, * m_pfMid, * m_pfLow, * m_pfLevel, * m_pfHPF, * m_pfInput, * m_pfOutput;
    float m_fSR;
    float lastIn1, lastOut1, lastIn2, lastOut2, lastIn3, lastOut3;
    float x1[3], x2[3], y1[3], y2[3];
} Mackity;

LADSPA_Handle instantiateMackity(const LADSPA_Descriptor * Descriptor, unsigned long SampleRate) {
    Mackity * ptr = (Mackity *)calloc(1, sizeof(Mackity));
    if (ptr) ptr->m_fSR = (float)SampleRate;
    return (LADSPA_Handle)ptr;
}

void connectPortMackity(LADSPA_Handle Instance, unsigned long Port, LADSPA_Data * Data) {
    Mackity * p = (Mackity *)Instance;
    if (!p) return;
    switch (Port) {
        case M_CRANK: p->m_pfCrank = Data; break;
        case M_SLAM:  p->m_pfSlam = Data; break;
        case M_HIGH:  p->m_pfHigh = Data; break;
        case M_MID:   p->m_pfMid = Data; break;
        case M_LOW:   p->m_pfLow = Data; break;
        case M_LEVEL: p->m_pfLevel = Data; break;
        case M_HPF:   p->m_pfHPF = Data; break;
        case M_INPUT: p->m_pfInput = Data; break;
        case M_OUTPUT: p->m_pfOutput = Data; break;
    }
}

void calcBiquad(float type, float freq, float sr, float db, float* b, float* a) {
    float A = powf(10.0f, db / 40.0f);
    float w0 = 2.0f * 3.14159265f * freq / sr;
    float alpha = sinf(w0) / (2.0f * 0.707f);
    float cosw = cosf(w0);
    float b0, b1, b2, a0, a1, a2;

    if (type == 0) { // High Shelf
        float rA = sqrtf(A);
        b0 = A*((A+1.0f)+(A-1.0f)*cosw+2.0f*rA*alpha);
        b1 = -2.0f*A*((A-1.0f)+(A+1.0f)*cosw);
        b2 = A*((A+1.0f)+(A-1.0f)*cosw-2.0f*rA*alpha);
        a0 = (A+1.0f)-(A-1.0f)*cosw+2.0f*rA*alpha;
        a1 = 2.0f*((A-1.0f)-(A+1.0f)*cosw);
        a2 = (A+1.0f)-(A-1.0f)*cosw-2.0f*rA*alpha;
    } else if (type == 1) { // Peaking
        b0 = 1.0f + alpha * A; b1 = -2.0f * cosw; b2 = 1.0f - alpha * A;
        a0 = 1.0f + alpha / A; a1 = -2.0f * cosw; a2 = 1.0f - alpha / A;
    } else { // Low Shelf
        float rA = sqrtf(A);
        b0 = A*((A+1.0f)-(A-1.0f)*cosw+2.0f*rA*alpha);
        b1 = 2.0f*A*((A-1.0f)-(A+1.0f)*cosw);
        b2 = A*((A+1.0f)-(A-1.0f)*cosw-2.0f*rA*alpha);
        a0 = (A+1.0f)+(A-1.0f)*cosw+2.0f*rA*alpha;
        a1 = -2.0f*((A-1.0f)+(A+1.0f)*cosw);
        a2 = (A+1.0f)+(A-1.0f)*cosw-2.0f*rA*alpha;
    }
    if (fabsf(a0) < 0.000001f) a0 = 1.0f;
    b[0]=b0/a0; b[1]=b1/a0; b[2]=b2/a0; a[1]=a1/a0; a[2]=a2/a0;
}

void runMackity(LADSPA_Handle Instance, unsigned long SampleCount) {
    Mackity * p = (Mackity *)Instance;
    float bH[3], aH[3], bM[3], aM[3], bL[3], aL[3];
    
    calcBiquad(0, 12000.0f, p->m_fSR, (*p->m_pfHigh - 0.5f)*30.0f, bH, aH);
    calcBiquad(1, 2500.0f, p->m_fSR, (*p->m_pfMid - 0.5f)*30.0f, bM, aM);
    calcBiquad(2, 80.0f, p->m_fSR, (*p->m_pfLow - 0.5f)*30.0f, bL, aL);

    float hpf_a = 1.0f / (1.0f + (2.0f * 3.14159265f * 75.0f / p->m_fSR));
    float crank = powf(10.0f, ((*p->m_pfCrank) * 36.0f) / 20.0f);
    float slam = (*p->m_pfSlam) * 1.57079633f;

    for (unsigned long i = 0; i < SampleCount; i++) {
        float s = p->m_pfInput[i] * crank;

        if (*p->m_pfHPF > 0.5f) {
            float o1 = hpf_a * (p->lastOut1 + s - p->lastIn1);
            p->lastIn1 = s; p->lastOut1 = o1;
            float o2 = hpf_a * (p->lastOut2 + o1 - p->lastIn2);
            p->lastIn2 = o1; p->lastOut2 = o2;
            s = hpf_a * (p->lastOut3 + o2 - p->lastIn3);
            p->lastIn3 = o2; p->lastOut3 = s;
        }

        float f[3][5] = {{bH[0],bH[1],bH[2],aH[1],aH[2]}, {bM[0],bM[1],bM[2],aM[1],aM[2]}, {bL[0],bL[1],bL[2],aL[1],aL[2]}};
        for(int j=0; j<3; j++){
            float out = f[j][0]*s + f[j][1]*p->x1[j] + f[j][2]*p->x2[j] - f[j][3]*p->y1[j] - f[j][4]*p->y2[j];
            p->x2[j]=p->x1[j]; p->x1[j]=s; p->y2[j]=p->y1[j]; p->y1[j]=out; s=out;
        }

        if (s > 1.0f) s = 1.0f; else if (s < -1.0f) s = -1.0f;
        p->m_pfOutput[i] = sinf(s * slam) * (*p->m_pfLevel);
    }
}

void cleanupMackity(LADSPA_Handle Instance) { free(Instance); }

static LADSPA_Descriptor * g_desc = NULL;
const LADSPA_Descriptor * ladspa_descriptor(unsigned long Index) {
    if (Index != 0) return NULL;
    if (!g_desc) {
        g_desc = (LADSPA_Descriptor *)calloc(1, sizeof(LADSPA_Descriptor));
        g_desc->UniqueID = 604012;
        g_desc->Label = strdup("mire_strip");
        g_desc->Name = strdup("Mire Strip");
        g_desc->Maker = strdup("Mire");
        g_desc->PortCount = 9;
        
        LADSPA_PortDescriptor * pd = (LADSPA_PortDescriptor *)calloc(9, sizeof(LADSPA_PortDescriptor));
        for(int i=0; i<7; i++) pd[i]=LADSPA_PORT_INPUT|LADSPA_PORT_CONTROL;
        pd[7]=LADSPA_PORT_INPUT|LADSPA_PORT_AUDIO; pd[8]=LADSPA_PORT_OUTPUT|LADSPA_PORT_AUDIO;
        g_desc->PortDescriptors = pd;

        const char ** names = (const char **)calloc(9, sizeof(char *));
        names[0]="Gain"; names[1]="Slam"; names[2]="Hi (12k)"; names[3]="Mid (2.5k)"; names[4]="Low (80Hz)"; names[5]="Level"; names[6]="75Hz HPF"; names[7]="In"; names[8]="Out";
        g_desc->PortNames = names;

        LADSPA_PortRangeHint * h = (LADSPA_PortRangeHint *)calloc(9, sizeof(LADSPA_PortRangeHint));
        
        h[M_CRANK].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_0;
        h[M_CRANK].LowerBound=0; h[M_CRANK].UpperBound=1;

        // FIXED: Using LADSPA_HINT_DEFAULT_1 instead of MAX
        h[M_SLAM].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_1;
        h[M_SLAM].LowerBound=0; h[M_SLAM].UpperBound=1;

        for(int i=2; i<=4; i++) {
            h[i].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_MIDDLE;
            h[i].LowerBound=0; h[i].UpperBound=1;
        }

        // FIXED: Using LADSPA_HINT_DEFAULT_1 instead of MAX
        h[M_LEVEL].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_BOUNDED_ABOVE|LADSPA_HINT_DEFAULT_1;
        h[M_LEVEL].LowerBound=0; h[M_LEVEL].UpperBound=1;

        h[M_HPF].HintDescriptor = LADSPA_HINT_TOGGLED|LADSPA_HINT_DEFAULT_0;
        
        g_desc->PortRangeHints = h;
        g_desc->instantiate = instantiateMackity; g_desc->connect_port = connectPortMackity; g_desc->run = runMackity; g_desc->cleanup = cleanupMackity;
    }
    return g_desc;
}
