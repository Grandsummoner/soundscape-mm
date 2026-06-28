#include "plugin.hpp"

// ============================================================
// Scales (Voltage Block manual p.11)
// ============================================================
static const float SCALES[16][12] = {
    {0,1,2,3,4,5,6,7,8,9,10,11},  {0,1,5,7,10,0,1,5,7,10,0,1},
    {0,2,4,7,9,0,2,4,7,9,0,2},    {0,3,5,7,10,0,3,5,7,10,0,3},
    {0,3,5,6,7,10,0,3,5,6,7,10},  {0,1,3,4,6,8,10,0,1,3,4,6},
    {0,2,4,5,6,8,10,0,2,4,5,6},   {0,1,3,5,7,8,10,0,1,3,5,7},
    {0,2,3,5,7,8,10,0,2,3,5,7},   {0,2,3,5,7,9,10,0,2,3,5,7},
    {0,2,4,5,7,9,10,0,2,4,5,7},   {0,1,4,5,7,8,11,0,1,4,5,7},
    {0,1,4,5,7,8,11,0,1,4,5,7},   {0,2,4,5,7,9,11,0,2,4,5,7},
    {0,2,4,6,7,9,11,0,2,4,6,7},   {0,1,2,3,4,5,6,7,8,9,10,11},
};
static const int SCALE_SIZES[16] = {12,5,5,5,6,7,7,7,7,7,7,7,7,7,7,12};

static float quantizeVoltage(float v, int scaleIdx) {
    if (scaleIdx == 0 || scaleIdx == 15) return v;
    float semitones = v * 12.0f;
    int octave = (int)std::floor(semitones / 12.0f);
    int semi   = (int)std::floor(semitones) - octave * 12;
    if (semi < 0) { semi += 12; octave--; }
    int sz = SCALE_SIZES[scaleIdx];
    int best = 0, bestDist = 12;
    for (int i = 0; i < sz; i++) {
        int d = std::abs(semi - (int)SCALES[scaleIdx][i]);
        if (d < bestDist) { bestDist = d; best = (int)SCALES[scaleIdx][i]; }
    }
    return (octave * 12 + best) / 12.0f;
}

// ============================================================
struct Soundscape : Module {
    // ... [Use existing logic from Skyline, replacing struct names as necessary] ...
    // Note: ensure internal enums/variables reference Soundscape not Skyline.
};

// ... [Keep SlimFader and EditRingLight logic unchanged, just ensure they are included] ...

struct SoundscapePort : app::SvgPort {
    SoundscapePort() {
        setSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/SoundscapeJack.svg")));
    }
};

struct SoundscapeWidget : ModuleWidget {
    SoundscapeWidget(Soundscape* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Soundscape.svg")));

        // MANDATORY FIX: Explicit bounding box for UI interaction
        box.size = Vec(20 * 15.f, 380.f); 

        const float cX[8]={7.00f,19.51f,32.03f,44.54f,57.06f,69.57f,82.09f,94.60f};
        const float xJack=7.00f,xSwitch=20.00f;
        const float xK1=32.0f,xK2=48.5f,xK3=65.0f;
        const float xB1=78.5f,xB2=87.0f,xB3=95.5f;
        const float yOut=22.5f,yLed=31.0f,yClk=46.0f,yKnob=53.5f,yRst=61.0f;
        const float yB1=46.0f,yB2=61.0f,ySld=70.0f,yS1=104.0f,yS2=119.0f;

        // ... [Include your loop and addChild logic here] ...
    }
};

Model* modelSoundscape = createModel<Soundscape, SoundscapeWidget>("SoundscapeMM");
